#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

extern "C" {
#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
#include <libswscale/swscale.h>
}
#include <x264.h>
#include <libavutil/pixfmt.h>

#include <SDL2/SDL.h>
#include <queue>


using namespace std;


typedef unsigned char u8;


//The following queues are thread safe only because of the underlying
//design and use case (single reader and single writer), so to avoid
//(in this case) needless overhead, these will not be protected by
//mutex locks.
bool parseBytesDone = false;
queue<unsigned char> *parseBytesOut;

bool bitsToColorDone = false;
queue<u8*> *bitsToColorOut;

bool renderFramesDone = false;
queue<SDL_Surface*> *renderFramesOut;

bool encodeToVideoDone = false;



void *parseBytes(void *ignore){
  while(!feof(stdin) && !ferror(stdin)){
    unsigned char byte = fgetc(stdin);
    parseBytesOut->push(((byte & 0x80) != 0) * 255);
    parseBytesOut->push(((byte & 0x40) != 0) * 255);
    parseBytesOut->push(((byte & 0x20) != 0) * 255);
    parseBytesOut->push(((byte & 0x10) != 0) * 255);
    parseBytesOut->push(((byte & 0x08) != 0) * 255);
    parseBytesOut->push(((byte & 0x04) != 0) * 255);
    parseBytesOut->push(((byte & 0x02) != 0) * 255);
    parseBytesOut->push(((byte & 0x01) != 0) * 255);
  }
  parseBytesDone = true;
  return NULL;
}


void *bitsToColor(void *ignore){
  int i;
  u8 *color = (u8*) malloc(sizeof(u8) * 3);
  
  i = 0;
  
  while(!parseBytesDone || !parseBytesOut->empty()){
    if(parseBytesOut->empty()) continue;
    
    color[i++] = parseBytesOut->front();
    parseBytesOut->pop();
    
    if(i >= 3){
      i = 0;
      bitsToColorOut->push(color);
      color = (u8*) malloc(sizeof(u8) * 3);
    }
     
  }
  
  if(i > 0){
    for(;i < 3; i++)  color[i] = 0;
    bitsToColorOut->push(color);
  }else{
    free(color);
  }
  
  bitsToColorDone = true;
  
  return NULL;
}


void *renderFrames(void *ignore){
  int x, y;
  u8 *color;
  SDL_Rect tmpRect;

  
  x = y = 0;
  tmpRect.h = 8;  tmpRect.w = 8;
  Uint32 r, g, b, a;
  #if SDL_BYTEORDER == SDL_BIG_ENDIAN
    r = 0xff000000;
    g = 0x00ff0000;
    b = 0x0000ff00;
    a = 0x000000ff;
#else
    r = 0x000000ff;
    g = 0x0000ff00;
    b = 0x00ff0000;
    a = 0xff000000;
#endif

  SDL_Surface *image = SDL_CreateRGBSurface(0, 256, 144, 32, r , g, b, a);
  
  while(!bitsToColorDone || !bitsToColorOut->empty()){
    if(bitsToColorOut->empty()) continue;
    
    color = bitsToColorOut->front();
    bitsToColorOut->pop();
    
    tmpRect.x = x;  tmpRect.y = y;
    SDL_FillRect(image, &tmpRect, SDL_MapRGB(image->format, color[0], color[1], color[2]));
    free(color);
    
    
    x += 8;
    if(x >= 256){
      x = 0;
      y += 8;
      if(y >= 144){
        y = 0;
        renderFramesOut->push(image);
        image = SDL_CreateRGBSurface(0, 256, 144, 32, r , g, b, a);
      }
    }
  }
  
  if(x || y){
    renderFramesOut->push(image);
  }else{
    SDL_FreeSurface(image);
  }
  
  renderFramesDone = true;
  
  
  
  return NULL;
}


//from https://github.com/FFmpeg/FFmpeg/blob/n3.0/doc/examples/decoding_encoding.c
void *encodeToVideo(void *ignore){
  
  AVCodec *codec;
  AVCodecContext *c= NULL;
  int i, ret, got_output;
  AVFrame *_frame;
  AVPacket pkt;
  uint8_t endcode[] = { 0, 0, 1, 0xb7 };
  i = 0;
  queue<SDL_Surface*> freeLater;

  /* find the video encoder */
  avcodec_register_all();
  codec = avcodec_find_encoder(AV_CODEC_ID_H264);
  if (!codec) {
    fprintf(stderr, "Codec not found\n");
    exit(1);
  }
    
  c = avcodec_alloc_context3(codec);
  if (!c) {
    fprintf(stderr, "Could not allocate video codec context\n");
    exit(1);
  }
    
  /* put sample parameters */
  c->bit_rate = 400000;
  c->width = 256;
  c->height = 144;
  /* frames per second */
  c->time_base = (AVRational){1,30};
  /* emit one intra frame every frames*/
  c->gop_size = 1;
  c->max_b_frames = 0;
  c->pix_fmt = AV_PIX_FMT_YUV420P;

  av_opt_set(c->priv_data, "zerolatency", "veryfast", 0);

  /* open codec */
  if (avcodec_open2(c, codec, NULL) < 0) {
    fprintf(stderr, "Could not open codec\n");
    exit(1);
  }

  _frame = av_frame_alloc();
  if (!_frame) {
      fprintf(stderr, "Could not allocate video frame\n");
      exit(1);
  }
  _frame->format = c->pix_fmt;
  _frame->width  = c->width;
  _frame->height = c->height;

  /* the image can be allocated by any means and av_image_alloc() is
  * just the most convenient way if av_malloc() is to be used */
  ret = av_image_alloc(_frame->data, _frame->linesize, c->width, c->height,
                         c->pix_fmt, 32);
  if (ret < 0) {
    fprintf(stderr, "Could not allocate raw picture buffer\n");
    exit(1);
  }
  
  while(!renderFramesDone || !renderFramesOut->empty()){
    if(renderFramesOut->empty()) continue;
    SDL_Surface *imageToEncode = renderFramesOut->front();
    renderFramesOut->pop();
    freeLater.push(imageToEncode);
    
    av_init_packet(&pkt);
    pkt.data = NULL;    // packet data will be allocated by the encoder
    pkt.size = 0;

    fflush(stdout);
    /*This may or may not work, depending on if the pixel formats 
    are the same*/
    
    
    struct SwsContext* convertCtx;
    convertCtx = sws_getContext(256, 144, AV_PIX_FMT_RGB24, 
                                256, 144, AV_PIX_FMT_YUV420P, 0, 0, 0, 0);
    
    uint8_t *inData[1] = {(uint8_t *)(imageToEncode->pixels)};
    int inLinesize[1] = { 256 };
    sws_scale(convertCtx, inData, inLinesize, 0, 144, _frame->data, _frame->linesize);
    sws_freeContext(convertCtx);
    
    _frame->pts = i++;

    /* encode the image */
    ret = avcodec_encode_video2(c, &pkt, _frame, &got_output);
    
    if (ret < 0) {
      fprintf(stdout, "Error encoding frame\n");
      exit(1);
    }

    if(got_output){
      fflush(stdout);
      fwrite(pkt.data, 1, pkt.size, stdout);
      av_packet_unref(&pkt);
    }
    
  }
  
  
  /* get the delayed frames */
  for (got_output = 1; got_output; i++) {
    fflush(stdout);

    ret = avcodec_encode_video2(c, &pkt, NULL, &got_output);
    if (ret < 0) {
      fprintf(stderr, "Error encoding frame\n");
      exit(1);
    }

    if (got_output) {
      fprintf(stderr, "Write frame %3d (size=%5d)\n", i, pkt.size);
      fwrite(pkt.data, 1, pkt.size, stdout);
      av_packet_unref(&pkt);
    }
  }
  

  /* add sequence end code to have a real mpeg file */
  fwrite(endcode, 1, sizeof(endcode), stdout);
  fclose(stdout);

  avcodec_close(c);
  av_free(c);
  av_freep(_frame->data);
  av_frame_free(&_frame);
  
  while(!freeLater.empty()){
    SDL_FreeSurface(freeLater.front());
    freeLater.pop();
  }
  
  encodeToVideoDone = true;
  return NULL;
}


void debugFreeQueues(){
  while(!bitsToColorOut->empty()){
    free(bitsToColorOut->front());
    bitsToColorOut->pop();
  }
  while(!renderFramesOut->empty()){
    SDL_FreeSurface(renderFramesOut->front());
    renderFramesOut->pop();
  }
}


int main(int argc, char **arhv){
  
  //pthread_t threads[5];
  //int statuses[5];
  //char fake;
  
  parseBytesOut = new queue<u8>();
  bitsToColorOut = new queue<u8*>();
  renderFramesOut = new queue<SDL_Surface*>();
  
  
  //Initialize SDL
  //if (SDL_Init(SDL_INIT_VIDEO) < 0)  {
  //  printf("SDL could not initialize! SDL Error: %s\n", SDL_GetError());
  //  exit(-1);
  //}
  //Set texture filtering to linear
  //SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
  
  //*
  //statuses[0] = pthread_create( &threads[0], NULL, parseBytes, (void*) &fake);
  //statuses[1] = pthread_create( &threads[1], NULL, bitsToFrame, (void*) &fake);
  //statuses[2] = pthread_create( &threads[2], NULL, addInteg, (void*) &fake);
  //statuses[3] = pthread_create( &threads[3], NULL, renderFrames, (void*) &fake);
  //statuses[4] = pthread_create( &threads[4], NULL, encodeToVideo, (void*) &fake);
  /* */
  
  
  parseBytes(NULL);
  bitsToColor(NULL);
  renderFrames(NULL);
  encodeToVideo(NULL);
  
  //for(int i = 0; i < 2; i++)
  //  pthread_join(threads[i], NULL);
  //SDL_Quit();
  
  delete parseBytesOut;
  delete bitsToColorOut;
  delete renderFramesOut;
  
  return 0;
}