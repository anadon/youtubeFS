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

//just to make allocating and deallocating easier
class frame{
  public:
  unsigned char pixels[127][71][3];
};



//The following queues are thread safe only because of the underlying
//design and use case (single reader and single writer), so to avoid
//(in this case) needless overhead, these will not be protected by
//mutex locks.
bool parseBytesDone = false;
queue<unsigned char> *parseBytesOut;

bool bitsToFrameDone = false;
queue<frame*> *bitsToFrameOut;

bool addIntegDone = false;
queue<frame*> *addIntegOut;

bool renderFramesDone = false;
queue<SDL_Surface*> *renderFramesOut;

bool encodeToVideoDone = false;

//taken from http://sdl.beuc.net/sdl.wiki/Pixel_Access
void putpixel(SDL_Surface *surface, int x, int y, Uint32 pixel){
    int bpp = surface->format->BytesPerPixel;
    /* Here p is the address to the pixel we want to set */
    Uint8 *p = (Uint8 *)surface->pixels + y * surface->pitch + x * bpp;

    switch(bpp) {
    case 1:
        *p = pixel;
        break;

    case 2:
        *(Uint16 *)p = pixel;
        break;

    case 3:
        if(SDL_BYTEORDER == SDL_BIG_ENDIAN) {
            p[0] = (pixel >> 16) & 0xff;
            p[1] = (pixel >> 8) & 0xff;
            p[2] = pixel & 0xff;
        } else {
            p[0] = pixel & 0xff;
            p[1] = (pixel >> 8) & 0xff;
            p[2] = (pixel >> 16) & 0xff;
        }
        break;

    case 4:
        *(Uint32 *)p = pixel;
        break;
    }
}









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


void *bitsToFrame(void *ignore){
  int i, j, k;
  frame *curFrame = new frame();
  
  i = j = k = 0;
  
  while(!parseBytesDone || !parseBytesOut->empty()){
    if(parseBytesOut->empty()) continue;
    
    curFrame->pixels[i][j][k] = parseBytesOut->front();
    parseBytesOut->pop();
    
    k++;
    if(k >= 3){
      k = 0;
      j++;
      if(j >= (71 - 1)){
        j = 0;
        i++;
        if(i >= (127 - 1)){
          i = 0;
          bitsToFrameOut->push(curFrame);
          curFrame = new frame();
        }
      }
    }
     
  }
  
  while(i || j || k){
    curFrame->pixels[i][j][k] = 0;
    k++;
    if(k >= 3){
      k = 0;
      j++;
      if(j >= (71 - 1)){
        j = 0;
        i++;
        if(i >= (127 - 1)){
          i = 0;
          bitsToFrameOut->push(curFrame);
          break;
        }
      }
    }
  }
  
  bitsToFrameDone = true;
  return NULL;
}


void *addInteg(void *ignore){
  while(!bitsToFrameDone || !bitsToFrameOut->empty()){
    if(bitsToFrameOut->empty()) continue;
    frame *toProcess = bitsToFrameOut->front();
    bitsToFrameOut->pop();
    
    //[127][71][3];
    for(int k = 0; k < 3; k++){
      bool csum;
      
      #pragma omp for
      for(int i = 0; i < (127-1); i++){
        csum = false;
        for(int j = 0; j < (71-1); j++){
          csum = csum ^ (toProcess->pixels[i][j][k] != 0);
        }
        toProcess->pixels[i][(71-1)][k] = (csum * 255);
      }
      
      #pragma omp for
      for(int j = 0; j < (71-1); j++){
        csum = false;
        for(int i = 0; i < (127-1); i++){
          csum = csum ^ (toProcess->pixels[i][j][k] != 0);
        }
        toProcess->pixels[(127-1)][j][k] = (csum * 255);
      }
      
      
      csum = false;
      for(int j = 0; j < (71-1); j++){
        csum = csum ^ (toProcess->pixels[(127-1)][j][k] != 0);
      }
      for(int i = 0; i < (127-1); i++){
        csum = csum ^ (toProcess->pixels[i][(71-1)][k] != 0);
      }
      toProcess->pixels[(127-1)][(71-1)][k] = (csum * 255);
    }
    
    addIntegOut->push(toProcess);
  }
  addIntegDone = true;
  return NULL;
}


typedef unsigned char u8;


Uint32 endianSafeBytemask(u8 r, u8 g, u8 b, u8 a){
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
  return (r << 24) & (g << 16) & (b << 8) & (a << 0);
#else
  return (r << 0) & (g << 8) & (b << 16) & (a << 24);
#endif
}

void *renderFrames(void *ignore){
  fprintf(stderr, SDL_GetError()); fflush(stderr);
  Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN | SDL_WINDOW_BORDERLESS;
  Uint32 rendererFlags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE;
  //Uint32 windowFlags = 0;
  //Uint32 rendererFlags = SDL_RENDERER_TARGETTEXTURE;
  
  
  SDL_Window *renderContext = SDL_CreateWindow("", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1, 1, windowFlags);
  fprintf(stderr, SDL_GetError()); fflush(stderr);
  
  SDL_Renderer *renderer = SDL_CreateRenderer(renderContext, -1, rendererFlags);
  fprintf(stderr, SDL_GetError()); fflush(stderr);
  
  //*
  while(!addIntegDone || !addIntegOut->empty()){
    if(addIntegOut->empty()) continue;
    
    frame *rawFrame = addIntegOut->front();
    addIntegOut->pop();
    
    
    SDL_Surface *image = SDL_CreateRGBSurface(0, 256, 144, 32, 0, 0, 0, 0xff);
    fprintf(stderr, SDL_GetError()); fflush(stderr);
    
    for(int i = 0; i < 127; i++){
      for(int j = 0; j < 71; j++){
        Uint32 newPixel;
        newPixel = endianSafeBytemask(rawFrame->pixels[i][j][0], 
                                      rawFrame->pixels[i][j][1], 
                                      rawFrame->pixels[i][j][2], 
                                      0xff);
        SDL_LockSurface(image);
        putpixel(image, 1 + 2*i, 1 + 2+j, newPixel);
        SDL_UnlockSurface(image);
      }
    }
    
    delete rawFrame;
    SDL_RenderClear( renderer );
    fprintf(stderr, SDL_GetError()); fflush(stderr);
    //SDL_Surface *YUVSurface = SDL_ConvertSurfaceFormat(image, SDL_PIXELFORMAT_IYUV, 0);
    //SDL_FreeSurface(image);
    renderFramesOut->push(image);
    
  }
  /* */
  renderFramesDone = true;
  
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(renderContext);
  fprintf(stderr, SDL_GetError()); fflush(stderr);
  
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
    
    for(int j = 0; j < 100; j++){
    av_init_packet(&pkt);
    pkt.data = NULL;    // packet data will be allocated by the encoder
    pkt.size = 0;

    fflush(stdout);
    /*This may or may not work, depending on if the pixel formats 
    are the same*/
    
    
    struct SwsContext* convertCtx;
    convertCtx = sws_getContext(256, 144, AV_PIX_FMT_RGB24, 256, 144, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, NULL, NULL, NULL);
    
    int tmp = 3*256;
    sws_scale(convertCtx, (const uint8_t* const*) &(imageToEncode->pixels), &tmp, 0, 144, _frame->data, _frame->linesize);
    
    
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
      fprintf(stderr, "Added image\n"); fflush(stderr);
    }
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
      fprintf(stderr, "Added image\n"); fflush(stderr);
    }
  }
  

  /* add sequence end code to have a real mpeg file */
  fwrite(endcode, 1, sizeof(endcode), stdout);
  fclose(stdout);

  avcodec_close(c);
  av_free(c);
  av_freep(&_frame->data[0]);
  av_frame_free(&_frame);
  
  while(!freeLater.empty()){
    SDL_DestroyTexture((SDL_Texture*) freeLater.front());
    freeLater.pop();
  }
  
  encodeToVideoDone = true;
  return NULL;
}


int main(int argc, char **arhv){
  
  //pthread_t threads[5];
  //int statuses[5];
  //char fake;
  
  parseBytesOut = new queue<unsigned char>();
  bitsToFrameOut = new queue<frame*>();
  addIntegOut = new queue<frame*>();
  renderFramesOut = new queue<SDL_Surface*>();
  
  
  //Initialize SDL
  if (SDL_Init(SDL_INIT_VIDEO) < 0)  {
    printf("SDL could not initialize! SDL Error: %s\n", SDL_GetError());
    exit(-1);
  }
  //Set texture filtering to linear
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
  
  //*
  //statuses[0] = pthread_create( &threads[0], NULL, parseBytes, (void*) &fake);
  //statuses[1] = pthread_create( &threads[1], NULL, bitsToFrame, (void*) &fake);
  //statuses[2] = pthread_create( &threads[2], NULL, addInteg, (void*) &fake);
  //statuses[3] = pthread_create( &threads[3], NULL, renderFrames, (void*) &fake);
  //statuses[4] = pthread_create( &threads[4], NULL, encodeToVideo, (void*) &fake);
  /* */
  
  
  parseBytes(NULL);
  bitsToFrame(NULL);
  addInteg(NULL);
  renderFrames(NULL);
  encodeToVideo(NULL);
  
  //for(int i = 0; i < 2; i++)
  //  pthread_join(threads[i], NULL);
  //SDL_Quit();
  
  delete parseBytesOut;
  delete bitsToFrameOut;
  delete addIntegOut;
  delete renderFramesOut;
  
  return 0;
}