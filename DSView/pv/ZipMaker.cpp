
/*
 * This file is part of the DSView project.
 * DSView is based on PulseView.
 *
 * Copyright (C) 2012 Joel Holdsworth <joel@airwebreathe.org.uk>
 * Copyright (C) 2013 DreamSourceLab <support@dreamsourcelab.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "ZipMaker.h" 
#include <assert.h>
#include <malloc.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "minizip/zip.h"
#include "minizip/unzip.h"

  
ZipMaker::ZipMaker() :
    m_zDoc(NULL)
{
    m_error[0] = 0; 
    m_opt_compress_level = Z_DEFAULT_COMPRESSION;
    m_zi = NULL;
}

ZipMaker::~ZipMaker()
{
    Release();
}

bool ZipMaker::CreateNew(const char *fileName, bool bAppend)
{
     assert(fileName);

     Release();
 
     m_zDoc = zipOpen64(fileName, bAppend); 
     if (m_zDoc == NULL) {
        strcpy(m_error, "zipOpen64 error");
    } 

//make zip inner file time 
    m_zi = new zip_fileinfo();

    time_t rawtime;
    time (&rawtime);
    struct tm *tinf= localtime(&rawtime);

    struct tm &ti = *tinf;
    zip_fileinfo &zi= *(zip_fileinfo*)m_zi;

    zi.tmz_date.tm_year = ti.tm_year;
    zi.tmz_date.tm_mon  = ti.tm_mon;
    zi.tmz_date.tm_mday = ti.tm_mday;
    zi.tmz_date.tm_hour = ti.tm_hour;
    zi.tmz_date.tm_min  = ti.tm_min;
    zi.tmz_date.tm_sec  = ti.tm_sec;
    zi.dosDate = 0;
      
    return m_zDoc != NULL;
}

void ZipMaker::Release()
{  
    if (m_zDoc){
       zipClose((zipFile)m_zDoc, NULL);
       m_zDoc = NULL;       
   }
   if (m_zi){
       delete ((zip_fileinfo*)m_zi);
       m_zi = NULL;
   }
}

bool ZipMaker::Close(){
    if (m_zDoc){
       zipClose((zipFile)m_zDoc, NULL);
       m_zDoc = NULL;
       return true;
   }
   return false;     
}

bool ZipMaker::AddFromBuffer(const char *innerFile, const char *buffer, unsigned int buferSize)
{
    assert(buffer);
    assert(innerFile);
    assert(m_zDoc);   
    int level = m_opt_compress_level;

    if (level < Z_DEFAULT_COMPRESSION  || level > Z_BEST_COMPRESSION){
        level = Z_DEFAULT_COMPRESSION;
    }

    zipOpenNewFileInZip((zipFile)m_zDoc,innerFile,(zip_fileinfo*)m_zi,
                                NULL,0,NULL,0,NULL ,
                                Z_DEFLATED,
                                level);

    zipWriteInFileInZip((zipFile)m_zDoc, buffer, (unsigned int)buferSize);

    zipCloseFileInZip((zipFile)m_zDoc);

    return true;
}

bool ZipMaker::AddFromFile(const char *localFile, const char *innerFile)
{
    assert(localFile);

    struct stat st;
    FILE *fp;
    char *data = NULL;
    long long size = 0;

    if ((fp = fopen(localFile, "rb")) == NULL) {
        strcpy(m_error, "fopen error");        
        return false;
    }

    if (fstat(fileno(fp), &st) < 0) {
        strcpy(m_error, "fstat error");    
        fclose(fp);
        return -1;
    } 

    if ((data = (char*)malloc((size_t)st.st_size)) == NULL) {
        strcpy(m_error, "can't malloc buffer");
        fclose(fp);
        return false;
    }

    if (fread(data, 1, (size_t)st.st_size, fp) < (size_t)st.st_size) {
        strcpy(m_error, "fread error");
        free(data);
        fclose(fp);
        return false;
    }

    fclose(fp);
    size = (size_t)st.st_size;

    bool ret = AddFromBuffer(innerFile, data, size);
    return ret;
}

const char *ZipMaker::GetError()
{
    if (m_error[0])
        return m_error;
    return NULL;
}


//------------------------ZipDecompress
  ZipDecompress::ZipDecompress()
  {
      m_uzDoc = NULL;
      m_curIndex = 0;
      m_fileCount = 0;
      m_bufferSize = 0;
      m_buffer = NULL;
  }

  ZipDecompress::~ZipDecompress()
  {
      Close();    
  }

  bool ZipDecompress::Open(const char *fileName)
  {
      assert(fileName);
      m_uzDoc = unzOpen64(fileName);

      if (m_uzDoc){
          m_uzi = new unz_file_info64();
          unz_global_info64 inf;
          unzGetGlobalInfo64((unzFile)m_uzDoc, &inf);
          m_fileCount = (int)inf.number_entry;
      }
      return m_uzDoc != NULL;
  }

  void ZipDecompress::Close()
  {
      if (m_uzDoc)
      {
          unzClose((unzFile)m_uzDoc);
          m_uzDoc = NULL;
      }
      if (m_uzi){
          delete ((unz_file_info64*)m_uzi);
          m_uzi = NULL;
      }
      if (m_buffer){
          free(m_buffer);
          m_buffer = NULL;
      }
  }

  bool ZipDecompress::ReadNextFileData(UnZipFileInfo &inf)
{
    assert(m_uzDoc);

    if (m_curIndex >= m_fileCount){
        strcpy(m_error, "read index is last");
        return false;
    }
    m_curIndex++;
 
    int ret = unzGetCurrentFileInfo64((unzFile)m_uzDoc, (unz_file_info64*)m_uzi, inf.inFileName, 256, NULL, 0, NULL, 0);
    if (ret != UNZ_OK){
        strcpy(m_error, "unzGetCurrentFileInfo64 error");
        return false;
     }
     unz_file_info64 &uzinf = *(unz_file_info64*)m_uzi;
     inf.dataLen = uzinf.uncompressed_size;
     inf.inFileNameLen = uzinf.size_filename;

     // need malloc memory buffer
     if (m_buffer == NULL || inf.dataLen > m_bufferSize){
         if (m_buffer) free(m_buffer);
         m_buffer = NULL;

         m_buffer = malloc(inf.dataLen + 10);
         if (m_buffer == NULL){
             strcpy(m_error, "malloc get null");
            return false;
         }
     }

     inf.pData = m_buffer; 

     unzOpenCurrentFile((unzFile)m_uzDoc);

     //read file data to buffer
     void *buf = inf.pData;
     long long buflen = inf.dataLen;
     long long rdlen = 0;

    while (rdlen < inf.dataLen)
    {
        int dlen = unzReadCurrentFile((unzFile)m_uzDoc, buf, buflen);
        if (dlen == 0){
            break;
        }
        rdlen += dlen;
        buf = buf + dlen; //move pointer
        buflen = inf.dataLen - rdlen;
    } 
 
     unzCloseCurrentFile((unzFile)m_uzDoc);
     unzGoToNextFile((unzFile)m_uzDoc);

     return true;
}
