#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
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


void *renderFrames(void *ignore){
  while(!addIntegDone || !addIntegOut->empty()){
    if(addIntegOut->empty()) continue;
    
    frame *rawFrame = addIntegOut->front();
    addIntegOut->pop();
    
    Uint32 amask;
    
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    amask = 0x000000ff;
#else
    amask = 0xff000000;
#endif
    
    SDL_Surface *image = SDL_CreateRGBSurface(0, 256, 144, 32, 0, 0, 0, amask);
    
    for(int i = 0; i < 127; i++){
      for(int j = 0; j < 71; j++){
        Uint32 newPixel;
        
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
        newPixel = amask & (rawFrame->pixels[i][j][0] << 24) & 
            (rawFrame->pixels[i][j][0] << 16) & (rawFrame->pixels[i][j][0] << 8);
#else
        newPixel = amask & (rawFrame->pixels[i][j][0] << 0) & 
            (rawFrame->pixels[i][j][0] << 8) & (rawFrame->pixels[i][j][0] << 16);
#endif
        putpixel(image, 1 + 2*i, 1 + 2+j, newPixel);
      }
    }
    
    delete rawFrame;
    
    renderFramesOut->push(image);
    
  }
  renderFramesDone = true;
  return NULL;
}


//Copied from https://github.com/FFmpeg/FFmpeg/blob/n3.0/doc/examples/decoding_encoding.c
void *encodeToVideo(void *ignore){
  while(!renderFramesDone || !renderFramesOut->empty()){
    if(renderFramesOut->empty()) continue;
    SDL_FreeSurface(renderFramesOut->front());
    renderFramesOut->pop();
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
    AVCodec *codec;
    AVCodecContext *c= NULL;
    int i, ret, x, y, got_output;
    FILE *f;
    AVFrame *_frame;
    AVPacket pkt;
    uint8_t endcode[] = { 0, 0, 1, 0xb7 };

    printf("Encode video file %s\n", filename);

    /* find the mpeg1 video encoder */
    codec = avcodec_find_encoder(codec_id);
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
    /* resolution must be a multiple of two */
    c->width = 256;
    c->height = 144;
    /* frames per second */
    c->time_base = (AVRational){1,30};
    /* emit one intra frame every ten frames
     * check frame pict_type before passing frame
     * to encoder, if frame->pict_type is AV_PICTURE_TYPE_I
     * then gop_size is ignored and the output of encoder
     * will always be I frame irrespective to gop_size
     */
    c->gop_size = 10;
    c->max_b_frames = 1;
    c->pix_fmt = AV_PIX_FMT_YUV420P;

    if (codec_id == AV_CODEC_ID_H264)
        av_opt_set(c->priv_data, "preset", "slow", 0);

    /* open it */
    if (avcodec_open2(c, codec, NULL) < 0) {
        fprintf(stderr, "Could not open codec\n");
        exit(1);
    }

    f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "Could not open %s\n", filename);
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

    /* encode 1 second of video */
    for (i = 0; i < 30; i++) {
        av_init_packet(&pkt);
        pkt.data = NULL;    // packet data will be allocated by the encoder
        pkt.size = 0;

        fflush(stdout);
        /* prepare a dummy image *///TODO this is where to add our data frames
        /* Y */
        for (y = 0; y < c->height; y++) {
            for (x = 0; x < c->width; x++) {
                _frame->data[0][y * _frame->linesize[0] + x] = x + y + i * 3;
            }
        }

        /* Cb and Cr */
        for (y = 0; y < c->height/2; y++) {
            for (x = 0; x < c->width/2; x++) {
                _frame->data[1][y * _frame->linesize[1] + x] = 128 + y + i * 2;
                _frame->data[2][y * _frame->linesize[2] + x] = 64 + x + i * 5;
            }
        }

        _frame->pts = i;

        /* encode the image */
        ret = avcodec_encode_video2(c, &pkt, _frame, &got_output);
        if (ret < 0) {
            fprintf(stderr, "Error encoding frame\n");
            exit(1);
        }

        if (got_output) {
            printf("Write frame %3d (size=%5d)\n", i, pkt.size);
            fwrite(pkt.data, 1, pkt.size, f);
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
            printf("Write frame %3d (size=%5d)\n", i, pkt.size);
            fwrite(pkt.data, 1, pkt.size, f);
            av_packet_unref(&pkt);
        }
    }

    /* add sequence end code to have a real mpeg file */
    fwrite(endcode, 1, sizeof(endcode), f);
    fclose(f);

    avcodec_close(c);
    av_free(c);
    av_freep(&_frame->data[0]);
    av_frame_free(&_frame);
    printf("\n");
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
    
  }
  encodeToVideoDone = true;
  return NULL;
}


int main(int argc, char **arhv){
  
  //pthread_t threads[5];
  int statuses[5];
  char fake;
  
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
  SDL_Quit();
  
  delete parseBytesOut;
  delete bitsToFrameOut;
  delete addIntegOut;
  delete renderFramesOut;
  
  return 0;
}