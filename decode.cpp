#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <string>

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


#define H264_INBUF_SIZE 16384 

using namespace std;


//The following queues are thread safe only because of the underlying
//design and use case (single reader and single writer), so to avoid
//(in this case) needless overhead, these will not be protected by
//mutex locks.

queue<u8> parseBytesIn;

queue<u8> bitsIn;

queue<u8*> FramesIn;


void parseBytes(){
  
  size_t i, writeSize;
  size_t breakSize_t[sizeof(size_t)];//technically not correct -- eh
  string fileName;
  
  while(parseBytesIn.front()){
    fileName += (const char)parseBytesIn.front();
    parseBytesIn.pop();
  }
  parseBytesIn.pop();
  
  //re-write file if a file was uploaded, else stream
  if(0 != fileName.length())
    freopen(fileName.c_str(), "wb", stdout);
  
  for(i = 0; i < sizeof(size_t); i++){
    breakSize_t[i] = parseBytesIn.front();
    parseBytesIn.pop();
  }
  
  writeSize = (( breakSize_t[7] << (8 * 0)) | 
              ( breakSize_t[6] << (8 * 1)) | 
              ( breakSize_t[5] << (8 * 2)) | 
              ( breakSize_t[4] << (8 * 3)) | 
              ( breakSize_t[3] << (8 * 4)) | 
              ( breakSize_t[2] << (8 * 5)) | 
              ( breakSize_t[1] << (8 * 6)) | 
              ( breakSize_t[0] << (8 * 7))) ;
  
  for(i = 0; i < writeSize; i++){
    fputc(parseBytesIn.front(), stdout);
    parseBytesIn.pop();
  }
  
  fclose(stdout);
}


void assembleBytes(){
  while(bitsIn.size() >= 8)  parseBytesIn.push(pullByte(bitsIn));
}


//TODO
void extractBitsFromFrames(){
  //int x, y;
  //u8 *color;
  //SDL_Rect tmpRect;

  
  //x = y = 0;
  //tmpRect.h = 8;  tmpRect.w = 8;
  //Uint32 r, g, b, a;
//#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    //r = 0xff000000;
    //g = 0x00ff0000;
    //b = 0x0000ff00;
    //a = 0x000000ff;
//#else
    //r = 0x000000ff;
    //g = 0x0000ff00;
    //b = 0x00ff0000;
    //a = 0xff000000;
//#endif

  //SDL_Surface *image = SDL_CreateRGBSurface(0, 256, 144, 32, r , g, b, a);
  
  //while(!FramesIn.empty()){
    
    //color = FramesIn.front();
    //FramesIn.pop();
    
    //tmpRect.x = x;  tmpRect.y = y;
    //SDL_FillRect(image, &tmpRect, SDL_MapRGB(image->format, color[0], color[1], color[2]));
    //free(color);
    
    
    //x += 8;
    //if(x >= 256){
      //x = 0;
      //y += 8;
      //if(y >= 144){
        //y = 0;
        //renderFramesOut.push(image);
        //image = SDL_CreateRGBSurface(0, 256, 144, 32, r , g, b, a);
      //}
    //}
  //}
  
  //if(x || y){
    //renderFramesOut.push(image);
  //}else{
    //SDL_FreeSurface(image);
  //}
  
}



void decodeVideoToFrames(){
  AVCodec *codec;
  AVCodecContext *c= NULL;
  AVFrame *frame;
  uint8_t inbuf[H264_INBUF_SIZE];
  AVPacket avpkt;
  AVCodecParserContext* parser;  
    
  avcodec_register_all();
  av_init_packet(&avpkt);

  /* set end of buffer to 0 (this ensures that no overreading happens for damaged mpeg streams) */
  //memset(inbuf + INBUF_SIZE, 0, AV_INPUT_BUFFER_PADDING_SIZE);


  codec = avcodec_find_decoder(AV_CODEC_ID_H264);

  c = avcodec_alloc_context3(codec);

  if (codec->capabilities & AV_CODEC_CAP_TRUNCATED)
    c->flags |= AV_CODEC_FLAG_TRUNCATED; // we do not send complete frames

  /* open it */
  avcodec_open2(c, codec, NULL);


  frame = av_frame_alloc();
  parser = av_parser_init(AV_CODEC_ID_H264);

  while(1){
    avpkt.size = fread(inbuf, H264_INBUF_SIZE, 1, stdin);
    if (avpkt.size == 0) break;
    
    avpkt.data = inbuf;
    while (avpkt.size > 0){
      
      int len, got_frame;

      len = avcodec_decode_video2(c, frame, &got_frame, &avpkt);
      if (len < 0) {
        fprintf(stderr, "Error while decoding frame \n");
        exit(1);
      }
      
      if (got_frame) {

        struct SwsContext* convertCtx;
        convertCtx = sws_getContext(256, 144, AV_PIX_FMT_YUV420P, 
                                  32,  18,  AV_PIX_FMT_RGB32, 0, 0, 0, 0);
    
        uint8_t *outData[1] = { (uint8_t*) malloc(32*18*4) };
        int outLinesize;
        sws_scale(convertCtx, frame->data, frame->linesize, 0, 18, outData, &outLinesize);
        sws_freeContext(convertCtx);
        
        FramesIn.push(outData[1]);
        
      }
      if (avpkt.data) {
        avpkt.size -= len;
        avpkt.data += len;
      }
    }
  }

  avpkt.data = NULL;
  avpkt.size = 0;
  //decode_write_frame(outfilename, c, frame, &frame_count, &avpkt, 1);

  fclose(stdin);

  avcodec_close(c);
  av_free(c);
  av_frame_free(&frame);
  av_parser_close(parser);
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
  
  struct fileWrapper outputFile;
  
  if(file){
    freopen(argv[1], "rb", stdin);
  }
  
  decodeVideoToFrames();
  extractBitsFromFrames();
  assembleBytes();
  parseBytes();
  
  
  return 0;
}
