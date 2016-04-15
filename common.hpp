#ifndef COMMON_HPP
#define COMMON_HPP

#include <queue>
#include <string.h>


typedef unsigned char u8;

struct fileWrapper{
  char *name;
  size_t fileSize;
  u8 *data;
};




inline void pushByte(u8 byte, std::queue<u8> &out){
    out.push(((byte & 0x80) != 0) * 255);
    out.push(((byte & 0x40) != 0) * 255);
    out.push(((byte & 0x20) != 0) * 255);
    out.push(((byte & 0x10) != 0) * 255);
    out.push(((byte & 0x08) != 0) * 255);
    out.push(((byte & 0x04) != 0) * 255);
    out.push(((byte & 0x02) != 0) * 255);
    out.push(((byte & 0x01) != 0) * 255);
}


inline u8 pullByte(std::queue<u8> &parseBytesIn){
  bool i0, i1, i2, i3, i4, i5, i6, i7;
  u8 tr = 0; //to return
  
  i0 = (parseBytesIn.front() != 0); parseBytesIn.pop();
  i1 = (parseBytesIn.front() != 0); parseBytesIn.pop();
  i2 = (parseBytesIn.front() != 0); parseBytesIn.pop();
  i3 = (parseBytesIn.front() != 0); parseBytesIn.pop();
  i4 = (parseBytesIn.front() != 0); parseBytesIn.pop();
  i5 = (parseBytesIn.front() != 0); parseBytesIn.pop();
  i6 = (parseBytesIn.front() != 0); parseBytesIn.pop();
  i7 = (parseBytesIn.front() != 0); parseBytesIn.pop();
  
  
  if(i0) tr &= (1 << 7);
  if(i1) tr &= (1 << 6);
  if(i2) tr &= (1 << 5);
  if(i3) tr &= (1 << 4);
  if(i4) tr &= (1 << 3);
  if(i5) tr &= (1 << 2);
  if(i6) tr &= (1 << 1);
  if(i7) tr &= (1 << 0);
  
  return tr;
}

#endif