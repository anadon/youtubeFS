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
#include <libavutil/pixfmt.h>
#include <libavutil/samplefmt.h>
#include <libswscale/swscale.h>
}
#include <x264.h>
#include <libavutil/pixfmt.h>

#include <SDL2/SDL.h>
#include <queue>
#include <sys/poll.h>

#include "common.hpp"


using namespace std;


//The following queues are thread safe only because of the underlying
//design and use case (single reader and single writer), so to avoid
//(in this case) needless overhead, these will not be protected by
//mutex locks.

queue<unsigned char> parseBytesOut;

queue<u8*> bitsToColorOut;

queue<SDL_Surface*> renderFramesOut;


void parseBytes(const struct fileWrapper fileData){
  
  size_t i = 0;
  u8 breakSize_t[sizeof(size_t)];//technically not correct -- eh
  
  if(NULL != fileData.name)
    while(fileData.name[i])
      pushByte(fileData.name[i++], parseBytesOut);
  pushByte(0, parseBytesOut);
  
  memcpy(breakSize_t, &(fileData.fileSize), sizeof(size_t));
  
  for(i = 0; i < sizeof(size_t); i++) pushByte(breakSize_t[i], parseBytesOut);
  
  for(i = 0; i < fileData.fileSize; i++) pushByte(fileData.data[i], parseBytesOut);
  
}


void bitsToColor(){
  int i;
  u8 *color = (u8*) malloc(sizeof(u8) * 3);
  
  i = 0;
  
  while(!parseBytesOut.empty()){
    if(parseBytesOut.empty()) continue;
    
    color[i++] = parseBytesOut.front();
    parseBytesOut.pop();
    
    if(i >= 3){
      i = 0;
      bitsToColorOut.push(color);
      color = (u8*) malloc(sizeof(u8) * 3);
    }
     
  }
  
  if(i > 0){
    for(;i < 3; i++)  color[i] = 0;
    bitsToColorOut.push(color);
  }else{
    free(color);
  }
  
}


void renderFrames(){
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
  
  while(!bitsToColorOut.empty()){
    
    color = bitsToColorOut.front();
    bitsToColorOut.pop();
    
    tmpRect.x = x;  tmpRect.y = y;
    SDL_FillRect(image, &tmpRect, SDL_MapRGB(image->format, color[0], color[1], color[2]));
    free(color);
    
    
    x += 8;
    if(x >= 256){
      x = 0;
      y += 8;
      if(y >= 144){
        y = 0;
        renderFramesOut.push(image);
        image = SDL_CreateRGBSurface(0, 256, 144, 32, r , g, b, a);
      }
    }
  }
  
  if(x || y){
    renderFramesOut.push(image);
  }else{
    SDL_FreeSurface(image);
  }
  
}


//from https://github.com/FFmpeg/FFmpeg/blob/n3.0/doc/examples/decoding_encoding.c
void encodeToVideo(){
  
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
  c->time_base = (AVRational){1,30}; /* frames per second */
  c->gop_size = 1; /* emit one intra frame every frames*/
  c->max_b_frames = 0;
  c->pix_fmt = AV_PIX_FMT_YUV420P;
  c->delay = 0;

  av_opt_set(c->priv_data, "zerolatency", "veryfast", 0);

  /* open codec */
  if (avcodec_open2(c, codec, NULL) < 0) {
    fprintf(stderr, "Could not open codec\n");
    exit(1);
  }
  
  _frame = av_frame_alloc();
  _frame->format = c->pix_fmt;
  _frame->width  = c->width;
  _frame->height = c->height;

  ret = av_image_alloc(_frame->data, _frame->linesize, c->width, 
      c->height, c->pix_fmt, 32);
  if (ret < 0) {
    fprintf(stderr, "Could not allocate raw picture buffer\n");
    exit(1);
  }
  
  while(!renderFramesOut.empty()){
    SDL_Surface *imageToEncode = renderFramesOut.front();
    renderFramesOut.pop();
    freeLater.push(imageToEncode);
    
    av_init_packet(&pkt);
    pkt.data = NULL;    // packet data will be allocated by the encoder
    pkt.size = 0;

    fflush(stdout);
    /*This may or may not work, depending on if the pixel formats 
    are the same*/
    
    
    struct SwsContext* convertCtx;
    convertCtx = sws_getContext(256, 144, AV_PIX_FMT_RGB32, 
                                256, 144, AV_PIX_FMT_YUV420P, 0, 0, 0, 0);
    
    uint8_t *inData[1] = {(uint8_t *)(imageToEncode->pixels)};
    int inLinesize[1] = { 4*256 };
    sws_scale(convertCtx, inData, inLinesize, 0, 144, _frame->data, _frame->linesize);
    sws_freeContext(convertCtx);
    
    _frame->pts = i++;

    /* encode the image */
    ret = avcodec_encode_video2(c, &pkt, _frame, &got_output);

    if(got_output){
      fflush(stdout);
      fwrite(pkt.data, 1, pkt.size, stdout);
      av_packet_unref(&pkt);
    }
    
  }
  
  
  /* get the delayed frames */
  got_output = 1;
  while (got_output) {

    ret = avcodec_encode_video2(c, &pkt, NULL, &got_output);

    if (got_output) {
      fflush(stdout);
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
  
}


//NOTE: the fileSize assignment is incorrect across different endian
//machines.
int main(int argc, char **argv){
  
  bool stream, file;
  struct pollfd pollStdin;
  pollStdin.fd = 0;
  pollStdin.events = POLLIN;
  
  stream = poll(&pollStdin, 1, 0);
  file = (argc == 2);
  
  if(!(stream ^ file)){
    fprintf(stderr, "need one file fron stdin xor path as arg\n");
    exit(-1);
  }
  
  struct fileWrapper input;
  
  if(stream){
    input.name = NULL;
    size_t i = 0;
    input.data = (u8*) malloc(i+1);
    int readChar;
    while(EOF != (readChar = fgetc(stdin))){
      input.data[i++] = (u8) readChar;
      input.data = (u8*) realloc(input.data, i+1);
    }
    input.fileSize = i-1;
  }else{
    FILE *fin = fopen(argv[1], "rb");
    if(NULL == fin) fprintf(stderr, "Unable to open specified file\n");
    
    input.name = argv[1];
    fseek(fin, 0, SEEK_END);
    input.fileSize = ftell(fin);
    rewind(fin);
    
    input.data = (u8*) malloc(input.fileSize);
    fread(input.data, 1, input.fileSize, fin);
    
    fclose(fin);
  }
  
  parseBytes(input);
  free(input.data);
  bitsToColor();
  renderFrames();
  encodeToVideo();
  
  
  return 0;
}