/* -*- c++ -*- */
/*
 * Copyright 2004 Free Software Foundation, Inc.
 *
 * This file is part of GNU Radio
 *
 * GNU Radio is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * GNU Radio is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Radio; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

/*
 * config.h is generated by configure.  It contains the results
 * of probing for features, options etc.  It should be the first
 * file included in your .cc file.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ais_ais_source_f.h>
#include <gr_io_signature.h>
#include <cstdio>
/*
 * Create a new instance of ais_ais_source_f and return
 * a boost shared_ptr.  This is effectively the public constructor.
 */
ais_ais_source_f_sptr
ais_make_ais_source_f (const std::vector<char> &data,int rusk, bool hold)                       // This is needed for python to access this class
{
  return ais_ais_source_f_sptr (new ais_ais_source_f (data,rusk,hold));
}

/*
 * The private constructor
 */
ais_ais_source_f::ais_ais_source_f (const std::vector<char> &data,int rusk, bool hold)
  : gr_sync_block ("ais_source_f",
                   gr_make_io_signature (0,0, 0),       // Number of minimum input ports, maximum output ports and size of each element (char = 1byte)
                   gr_make_io_signature (1,1,sizeof(float))),
    d_offset(0),
    d_state(ST_START),
    d_nstart(0),
    ANTALLFOR(rusk)
    // Initializing class members   d_x for horizontal position and d_y for vertical position
{
  nypakke(data);
  d_npreamble = 0;
  d_nstartsign = 0;
  d_ndata = 0;
  d_ncrc = 0;
  d_nstopsign = 0;
  d_nslutt = 0;
  d_last = 1;
  d_plast = 1;
  d_hold = hold;
  d_bitstuffed = 0;
  d_datalast = 0;
  d_antallenner = 0;
  calculate_crc();
}

unsigned short ais_ais_source_f::sdlc_crc(unsigned char *data, unsigned len)
{
  unsigned short c, crc=0xffff;

  while(len--)
    for(c = 0x100 + *data++; c> 1 ; c >>=1)
      if((crc ^ c) & 1)
        crc=(crc>>1)^0x8408;
      else
        crc>>=1;
  return ~crc;

}

void ais_ais_source_f::calculate_crc()
{
  int antallbytes = d_data.size() / 8;
  unsigned char *data = (unsigned char*) malloc(sizeof(unsigned char)*antallbytes);
  int i,j;

  unsigned char tmp;

  for(j=0;j<antallbytes;j++)
    {
      tmp = 0;
      for(i=0;i<8;i++)
        tmp |= (((d_data[i+8*j]) << (i)));
      data[j] = tmp;
    }
  unsigned short crc = sdlc_crc(data,antallbytes);
  //fprintf(stderr,"%04x\n",crc);
  free(data);

  for(i=0;i<16;i++)
    {
      d_crc[i] = (crc >> i) & 1;
      //printf("%d",d_crc[i]);
    }

  //printf("\n");

}

void ais_ais_source_f::nypakke (const std::vector<char> &data)
{
  int i,j;
  d_data.clear();
  printf("datasize:%d\n", data.size());
  for(i=0;i<(data.size()/8);i++)
    {
      for(j=0;j<8;j++)
        {
          d_data.push_back(data[i*8+(7-j)]);
        }

    }
  restart();
}

void ais_ais_source_f::restart ()
{
  gruel::scoped_lock l(d_mutex);
  d_state = ST_START;
  d_ndata = 0;
  d_nstart = 0;
  d_npreamble = 0;
  d_nstartsign = 0;
  d_ncrc = 0;
  d_nstopsign = 0;
  d_nslutt = 0;
  calculate_crc();
}

ais_ais_source_f::~ais_ais_source_f ()
{
}

int
ais_ais_source_f::work (int noutput_items,
                        gr_vector_const_void_star &input_items,
                        gr_vector_void_star &output_items)
{
  float *out = (float *) output_items[0];
  gruel::scoped_lock l(d_mutex);
  unsigned int size = d_data.size();
  unsigned n  = 0;
  int bitstuffed = 0;
  if (size == 0) return 0;
  unsigned char tmp=0;
  //   enum state_t { ST_START, ST_PREAMBLE, ST_STARTSIGN, ST_DATA, ST_CRC, ST_STOPSIGN, ST_SLUTT};
  //
  switch (d_state){

  case  ST_DATA:
    if (d_ndata >= size) {
      d_state = ST_CRC;
      //printf("CRC\n");
      d_ndata = 0;
      return 0;
    }
    n = std::min (((unsigned)size) - d_ndata, (unsigned) noutput_items);

    for (unsigned int i = 0; i < n; i++) {
      if ((d_data[d_ndata + i]) == 0){
        d_last *= -1;
        d_antallenner = 0;
      }
      else {
        d_antallenner++;
        if(d_antallenner == 5)
          {
            //            printf("bitstuff");
            d_antallenner = 0;
            out[i+bitstuffed] = d_last;
            d_last *= -1;
            out[i+1+bitstuffed] = d_last;
            bitstuffed++;
          }
      }
      out[i+bitstuffed] = d_last;
      //printf("%.3f ",out[i]);
    }
    d_bitstuffed += bitstuffed;
    d_ndata += n;
    return n + bitstuffed;
    break;
  case ST_START:
    if (d_nstart >= ANTALLFOR)
      {
        d_state = ST_PREAMBLE;
        d_nstart = 1;
        //printf("Preamble\n");
        return 0;
      }
    n = std::min ((unsigned)ANTALLFOR - d_nstart, (unsigned) noutput_items);

    for(unsigned int i=0;i<n;i++)
      out[i] = 0;

    d_nstart += n;
    return n;
    break;
  case ST_PREAMBLE:
    if (d_npreamble >= 25)
      {
        d_state = ST_STARTSIGN;
        d_npreamble = 0;
        //printf("Startsign\n");
        return 0;
      }
    n = std::min ((unsigned)25  - d_npreamble, (unsigned) noutput_items);
    for(unsigned int i=0;i<n;i++){
      d_plast *= -1;
      if (d_plast == -1) d_last *= -1;
      out[i] = d_last;
    }
    d_npreamble += n;
    return n;
    break;
  case ST_STARTSIGN:
    if (d_nstartsign >= 8)
      {
        d_state = ST_DATA;
        //printf("Data\n");
        d_nstartsign = 0;
        return 0;
      }
    n = std::min ((unsigned) 8 - d_nstartsign, (unsigned) noutput_items);
    for(unsigned int i=0; i<n;i++)
      {
        if(d_nstartsign == 0) tmp = 0;
        else if (d_nstartsign > 0 && d_nstartsign < 7) tmp = 1;
        else if (d_nstartsign == 7) tmp = 0;
        if (tmp == 0) d_last *= -1;
        out[i] = d_last;
        d_nstartsign++;
      }
    return n;
    break;
  case ST_CRC:
    if (d_ncrc >= 16)
      {
        d_state = ST_STOPSIGN;
        //printf("Stoppsign\n");
        d_ncrc = 0;
        return 0;
      }
    n = std::min ((unsigned) 16 - d_ncrc, (unsigned) noutput_items);
    for(unsigned int i=0;i<n;i++)
      {
        if ((d_crc[d_ncrc + i]) == 0){
          d_last *= -1;
          d_antallenner = 0;
        }
        else {
          d_antallenner++;
          if(d_antallenner == 5)
            {

              d_antallenner = 0;
              out[i+bitstuffed] = d_last;
              d_last *= -1;
              out[i+1+bitstuffed] = d_last;
              bitstuffed++;
            }
        }
        out[i+bitstuffed] = d_last;
      }
    d_bitstuffed += bitstuffed;
    d_ncrc += n;
    return n + bitstuffed;
    break;
  case ST_STOPSIGN:
    if (d_nstopsign >= 8)
      {
        d_state = ST_SLUTT;
        //printf("Slutt\n");
        d_nstopsign = 0;
        return 0;
      }
    n = std::min ((unsigned) 8 - d_nstopsign, (unsigned) noutput_items);
    for(unsigned int i=0; i<n;i++)
      {
        if(d_nstopsign == 0) tmp = 0;
        else if (d_nstopsign > 0 && d_nstopsign < 7) tmp = 1;
        else if (d_nstopsign == 7) tmp = 0;
        if (tmp == 0) d_last *= -1;
        out[i] = d_last;
        d_nstopsign++;
      }
    return n;
    break;
  case  ST_SLUTT:
    if (d_nslutt >= ANTALLFOR && !d_hold)
      {
        return -1;
      }
    n = std::min ((unsigned)ANTALLFOR - d_nslutt, (unsigned) noutput_items);
    for(unsigned int i=0;i<n;i++)
      {
        out[i] = 1;

      }
    d_nslutt += n;
    return n;
    break;
  default:
    return n;


  }
}

