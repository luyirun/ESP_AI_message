/**
 * @FilePath     : /ESP_AI/src/Base64_Arturo.h
 * @Description  :  
 * @Author       : 董文捷
 * @Version      : 0.0.1
 * @LastEditors  : 董文捷
 * @LastEditTime : 2025-07-03 21:30:16
 * @Copyright    : G AUTOMOBILE RESEARCH INSTITUTE CO.,LTD Copyright (c) 2025.
**/
/*
Copyright (C) 2016 Arturo Guadalupi. All right reserved.

This library is free software; you can redistribute it and/or modify it under the terms of the GNU Lesser General Public License as published by the Free Software Foundation; either version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.
*/

#ifndef _BASE64_H
#define _BASE64_H

class Base64Class{
  public:
    int encode(char *output, char *input, int inputLength);
    int decode(char * output, char * input, int inputLength);
    int encodedLength(int plainLength);
    int decodedLength(char * input, int inputLength);

  private:
    inline void fromA3ToA4(unsigned char * A4, unsigned char * A3);
    inline void fromA4ToA3(unsigned char * A3, unsigned char * A4);
    inline unsigned char lookupTable(char c);
};
extern Base64Class Base64_Arturo;

#endif // _BASE64_H
