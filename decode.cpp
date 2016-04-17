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

queue<u8> parseBytesIn;

queue<u8> bitsIn;

queue<SDL_Surface*> FramesIn;


void parseBytes(){
  
  size_t i, *writeSize;
  u8 breakSize_t[sizeof(size_t)];//technically not correct -- eh
  string fileName;
  
  while(parseBytesIn.front()){
    fileName += string((char)parseBytesIn.front());
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
  
  writeSize = &breakSize_t;
  
  for(i = 0; i < *writeSize; i++){
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


//TODO
//from https://github.com/FFmpeg/FFmpeg/blob/n3.0/doc/examples/decoding_encoding.c
void decodeVideoToFrames(){
  
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
  //~ c->bit_rate = 400000;
  //~ c->width = 256;
  //~ c->height = 144;
  //~ c->time_base = (AVRational){1,30}; /* frames per second */
  //~ c->gop_size = 1; /* emit one intra frame every frames*/
  //~ c->max_b_frames = 0;
  //~ c->pix_fmt = AV_PIX_FMT_YUV420P;
  //~ c->delay = 0;

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
	while(av_read_frame(c , *pkt)){
		avcodec_decode_video2( c, _frame, got_output, pkt)
		//addframe to queue
		
		
	}
  //~ ret = av_image_alloc(_frame->data, _frame->linesize, c->width, 
      //~ c->height, c->pix_fmt, 32);
  //~ if (ret < 0) {
    //~ fprintf(stderr, "Could not allocate raw picture buffer\n");
    //~ exit(1);
  //~ }
  //~ 
  //~ while(!renderFramesOut.empty()){
    //~ SDL_Surface *imageToEncode = renderFramesOut.front();
    //~ renderFramesOut.pop();
    //~ freeLater.push(imageToEncode);
    //~ 
    //~ av_init_packet(&pkt);
    //~ pkt.data = NULL;    // packet data will be allocated by the encoder
    //~ pkt.size = 0;
//~ 
    //~ fflush(stdout);
    //~ /*This may or may not work, depending on if the pixel formats 
    //~ are the same*/
    //~ 
    //~ 
    //~ struct SwsContext* convertCtx;
    //~ convertCtx = sws_getContext(256, 144, AV_PIX_FMT_RGB32, 
                                //~ 256, 144, AV_PIX_FMT_YUV420P, 0, 0, 0, 0);
    //~ 
    //~ uint8_t *inData[1] = {(uint8_t *)(imageToEncode->pixels)};
    //~ int inLinesize[1] = { 4*256 };
    //~ sws_scale(convertCtx, inData, inLinesize, 0, 144, _frame->data, _frame->linesize);
    //~ sws_freeContext(convertCtx);
    //~ 
    //~ _frame->pts = i++;
//~ 
    //~ /* encode the image */
    //~ ret = avcodec_encode_video2(c, &pkt, _frame, &got_output);
//~ 
    //~ if(got_output){
      //~ fflush(stdout);
      //~ fwrite(pkt.data, 1, pkt.size, stdout);
      //~ av_packet_unref(&pkt);
    //~ }
    //~ 
  //~ }
  
  
  /* get the delayed frames */
  //~ got_output = 1;
  //~ while (got_output) {
//~ 
    //~ ret = avcodec_encode_video2(c, &pkt, NULL, &got_output);
//~ 
    //~ if (got_output) {
      //~ fflush(stdout);
      //~ fwrite(pkt.data, 1, pkt.size, stdout);
      //~ av_packet_unref(&pkt);
    //~ }
  //~ }
  //~ 
//~ 
  //~ /* add sequence end code to have a real mpeg file */
  //~ fwrite(endcode, 1, sizeof(endcode), stdout);
  //~ fclose(stdout);
//~ 
  //~ avcodec_close(c);
  //~ av_free(c);
  //~ av_freep(_frame->data);
  //~ av_frame_free(&_frame);
  //~ 
  //~ while(!freeLater.empty()){
    //~ SDL_FreeSurface(freeLater.front());
    //~ freeLater.pop();
  //~ }
  
}






static void video_decode_example(const char *outfilename, const char *filename)
{
    AVCodec *codec;
    AVCodecContext *c= NULL;
    int frame_count;
    FILE *f;
    AVFrame *frame;
    uint8_t inbuf[INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
    AVPacket avpkt;

    av_init_packet(&avpkt);

    /* set end of buffer to 0 (this ensures that no overreading happens for damaged mpeg streams) */
    memset(inbuf + INBUF_SIZE, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    printf("Decode video file %s to %s\n", filename, outfilename);

    /* find the mpeg1 video decoder */
    codec = avcodec_find_decoder(AV_CODEC_ID_MPEG1VIDEO);
    if (!codec) {
        fprintf(stderr, "Codec not found\n");
        exit(1);
    }

    c = avcodec_alloc_context3(codec);
    if (!c) {
        fprintf(stderr, "Could not allocate video codec context\n");
        exit(1);
    }

    if (codec->capabilities & AV_CODEC_CAP_TRUNCATED)
        c->flags |= AV_CODEC_FLAG_TRUNCATED; // we do not send complete frames

    /* For some codecs, such as msmpeg4 and mpeg4, width and height
       MUST be initialized there because this information is not
       available in the bitstream. */

    /* open it */
    if (avcodec_open2(c, codec, NULL) < 0) {
        fprintf(stderr, "Could not open codec\n");
        exit(1);
    }

    f = stdin;
    if (!f) {
        fprintf(stderr, "Could not open %s\n", filename);
        exit(1);
    }

    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }

    frame_count = 0;
    for (;;) {
        avpkt.size = fread(inbuf, 1, INBUF_SIZE, f);
        if (avpkt.size == 0)
            break;

        /* NOTE1: some codecs are stream based (mpegvideo, mpegaudio)
           and this is the only method to use them because you cannot
           know the compressed data size before analysing it.
           BUT some other codecs (msmpeg4, mpeg4) are inherently frame
           based, so you must call them with all the data for one
           frame exactly. You must also initialize 'width' and
           'height' before initializing them. */

        /* NOTE2: some codecs allow the raw parameters (frame size,
           sample rate) to be changed at any frame. We handle this, so
           you should also take care of it */

        /* here, we use a stream based decoder (mpeg1video), so we
           feed decoder and see if it could decode a frame */
        avpkt.data = inbuf;
        while (avpkt.size > 0)
            if (decode_write_frame(outfilename, c, frame, &frame_count, &avpkt, 0) < 0)
                exit(1);
    }

    /* some codecs, such as MPEG, transmit the I and P frame with a
       latency of one frame. You must do the following to have a
       chance to get the last frame of the video */
    avpkt.data = NULL;
    avpkt.size = 0;
    decode_write_frame(outfilename, c, frame, &frame_count, &avpkt, 1);

    fclose(f);

    avcodec_close(c);
    av_free(c);
    av_frame_free(&frame);
    printf("\n");
}





static int decode_write_frame(const char *outfilename, AVCodecContext *avctx,
                              AVFrame *frame, int *frame_count, AVPacket *pkt, int last)
{
    int len, got_frame;
    char buf[1024];

    len = avcodec_decode_video2(avctx, frame, &got_frame, pkt);
    if (len < 0) {
        fprintf(stderr, "Error while decoding frame %d\n", *frame_count);
        return len;
    }
    if (got_frame) {
        printf("Saving %sframe %3d\n", last ? "last " : "", *frame_count);
        fflush(stdout);

		struct SwsContext* convertCtx;
		convertCtx = sws_getContext(256, 144, AV_PIX_FMT_YUV420P, 
									256, 144, AV_PIX_FMT_RGB32, 0, 0, 0, 0);
    
		uint8_t *outData[1] = malloc(144*256*4);
		int outLinesize;
		sws_scale(convertCtx, frame->data, frame->linesize, 0, 144, outData, &outLinesize);
		sws_freeContext(convertCtx);

        /* the picture is allocated by the decoder, no need to free it */
        snprintf(buf, sizeof(buf), outfilename, *frame_count);
        pgm_save(frame->data[0], frame->linesize[0],
                 frame->width, frame->height, buf);
        (*frame_count)++;
    }
    if (pkt->data) {
        pkt->size -= len;
        pkt->data += len;
    }
    return 0;
}

int 	sws_scale (struct SwsContext *c, const uint8_t *const srcSlice[], const int srcStride[], int srcSliceY, int srcSliceH, uint8_t *const dst[], const int dstStride[])
 	Scale the image slice in srcSlice and put the resulting scaled slice in the image in dst. 



//NOTE: the fileSize assignment is incorrect across different endian
//machines.
int main(int argc, char **argv){
  
  FILE *input;
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
