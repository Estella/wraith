/*
 * crypt.c -- handles:
 * psybnc crypt() 
 * File encryption
 *
 * $Id$
 */


#include "common.h"
#include "crypt.h"
#include "salt.h"
#include "misc.h"
#include "src/crypto/crypto.h"
#include <stdarg.h>

#define CRYPT_BLOCKSIZE AES_BLOCK_SIZE
#define CRYPT_KEYBITS 256
#define CRYPT_KEYSIZE (CRYPT_KEYBITS / 8)

AES_KEY e_key, d_key;

char *encrypt_binary(const char *keydata, unsigned char *data, int *datalen)
{
  int newdatalen = *datalen, blockcount = 0, blockndx = 0;
  unsigned char *newdata = NULL;

  /* First pad indata to CRYPT_BLOCKSIZE multiplum */
  if (newdatalen % CRYPT_BLOCKSIZE)             /* more than 1 block? */
    newdatalen += (CRYPT_BLOCKSIZE - (newdatalen % CRYPT_BLOCKSIZE));

  newdata = (unsigned char *) calloc(1, newdatalen);
  egg_memcpy(newdata, data, *datalen);
  if (newdatalen != *datalen)
    egg_bzero((void *) &newdata[*datalen], (newdatalen - *datalen));
  *datalen = newdatalen;

  if ((!keydata) || (!keydata[0])) {
    /* No key, no encryption */
    egg_memcpy(newdata, data, newdatalen);
  } else {
    char key[CRYPT_KEYSIZE + 1] = "";

    strncpyz(key, keydata, sizeof(key));
/*      strncpyz(&key[sizeof(key) - strlen(keydata)], keydata, sizeof(key)); */
    AES_set_encrypt_key(key, CRYPT_KEYBITS, &e_key);

    /* Now loop through the blocks and crypt them */
    blockcount = newdatalen / CRYPT_BLOCKSIZE;
    for (blockndx = blockcount - 1; blockndx >= 0; blockndx--) {
      AES_encrypt(&newdata[blockndx * CRYPT_BLOCKSIZE], &newdata[blockndx * CRYPT_BLOCKSIZE], &e_key);
    }
  }
  return newdata;
}

char *decrypt_binary(const char *keydata, unsigned char *data, int datalen)
{
  int blockcount = 0, blockndx = 0;
  unsigned char *newdata = NULL;

  datalen -= datalen % CRYPT_BLOCKSIZE;
  newdata = (unsigned char *) malloc(datalen);
  egg_memcpy(newdata, data, datalen);

  if ((!keydata) || (!keydata[0])) {
    /* No key, no decryption */
  } else {
    /* Init/fetch key */
    char key[CRYPT_KEYSIZE + 1] = "";

    strncpyz(key, keydata, sizeof(key));
/*      strncpy(&key[sizeof(key) - strlen(keydata)], keydata, sizeof(key)); */
    AES_set_decrypt_key(key, CRYPT_KEYBITS, &d_key);

    /* Now loop through the blocks and crypt them */
    blockcount = datalen / CRYPT_BLOCKSIZE;

    for (blockndx = blockcount - 1; blockndx >= 0; blockndx--) {
      AES_decrypt(&newdata[blockndx * CRYPT_BLOCKSIZE], &newdata[blockndx * CRYPT_BLOCKSIZE], &d_key);
    }

  }

  return newdata;
}

const char base64[64] = ".\\0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
const char base64r[256] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0, 0, 0, 0, 0, 0,
  0, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
  27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 0, 1, 0, 0, 0,
  0, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52,
  53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

char *encrypt_string(const char *keydata, char *data)
{
  int l, i, t;
  unsigned char *bdata = NULL;
  char *res = NULL;

  l = strlen(data) + 1;
  bdata = encrypt_binary(keydata, data, &l);
  if ((keydata) && (keydata[0])) {
    res = malloc((l * 4) / 3 + 5);
#define DB(x) ((unsigned char) (x+i<l ? bdata[x+i] : 0))
    for (i = 0, t = 0; i < l; i += 3, t += 4) {
      res[t] = base64[DB(0) >> 2];
      res[t + 1] = base64[((DB(0) & 3) << 4) | (DB(1) >> 4)];
      res[t + 2] = base64[((DB(1) & 0x0F) << 2) | (DB(2) >> 6)];
      res[t + 3] = base64[(DB(2) & 0x3F)];
    }
#undef DB
    res[t] = 0;
    free(bdata);
    return res;
  } else {
    return bdata;
  }
}

char *decrypt_string(const char *keydata, char *data)
{
  int i, l, t;
  char *buf = NULL, *res = NULL;

  l = strlen(data);
  if ((keydata) && (keydata[0])) {
    buf = malloc((l * 3) / 4 + 6);
#define DB(x) ((unsigned char) (x+i<l ? base64r[(unsigned char) data[x+i]] : 0))
    for (i = 0, t = 0; i < l; i += 4, t += 3) {
      buf[t] = (DB(0) << 2) + (DB(1) >> 4);
      buf[t + 1] = ((DB(1) & 0x0F) << 4) + (DB(2) >> 2);
      buf[t + 2] = ((DB(2) & 3) << 6) + DB(3);
    };
#undef DB
    t += 3;
    t -= (t % 4);
    res = decrypt_binary(keydata, buf, t);
    free(buf);
    return res;
  } else {
    res = malloc(l + 1);
    strcpy(res, data);
    return res;
  }
}

void encrypt_pass(char *s1, char *s2)
{
  char *tmp = NULL;

  if (strlen(s1) > 15)
    s1[15] = 0;
  tmp = encrypt_string(s1, s1);
  strcpy(s2, "+");
  strncat(s2, tmp, 15);
  s2[15] = 0;
  free(tmp);
}

int lfprintf (FILE *stream, ...)
{
  va_list va;
  char buf[8192], *ln = NULL, *nln = NULL, *tmp = NULL, *format = NULL;
  int res;

  buf[0] = 0;
  va_start(va, stream);
  format = va_arg(va, char *);
  egg_vsnprintf(buf, sizeof buf, format, va);
  va_end(va);

  ln = buf;
  while ((ln) && (ln[0])) {
    nln = strchr(ln, '\n');
    if (nln)
      *nln++ = 0;
    tmp = encrypt_string(SALT1, ln);
    res = fprintf(stream, "%s\n", tmp);
    free(tmp);
    if (res == EOF)
      return EOF;
    ln = nln;
  }
  return 0;
}

void Encrypt_File(char *infile, char *outfile)
{
  char  buf[8192];
  FILE *f = NULL, *f2 = NULL;
  int std = 0;

  if (!strcmp(outfile, "STDOUT"))
    std = 1;
  f  = fopen(infile, "r");
  if(!f)
    return;
  if (!std) {
    f2 = fopen(outfile, "w");
    if (!f2)
      return;
  } else {
    printf("----------------------------------START----------------------------------\n");
  }

  buf[0] = 0;
  while (fscanf(f, "%[^\n]\n", buf) != EOF) {
    if (std)
      printf("%s\n", encrypt_string(SALT1, buf));
    else
      lfprintf(f2, "%s\n", buf);
  }
  if (std)
    printf("-----------------------------------END-----------------------------------\n");

  fclose(f);
  if (f2)
    fclose(f2);
}

void Decrypt_File(char *infile, char *outfile)
{
  char buf[8192], *temps = NULL;
  FILE *f = NULL, *f2 = NULL;
  int std = 0;

  if (!strcmp(outfile, "STDOUT"))
    std = 1;
  f  = fopen(infile, "r");
  if (!f)
    return;
  if (!std) {
    f2 = fopen(outfile, "w");
    if (!f2)
      return;
  } else {
    printf("----------------------------------START----------------------------------\n");
  }
  
  buf[0] = 0;
  while (fscanf(f, "%[^\n]\n", buf) != EOF) {
    temps = (char *) decrypt_string(SALT1, buf);
    if (!std)
      fprintf(f2, "%s\n",temps);
    else
      printf("%s\n", temps);
    free(temps);
  }
  if (std)
    printf("-----------------------------------END-----------------------------------\n");

  fclose(f);
  if (f2)
    fclose(f2);
}


char *MD5(const char *string) 
{
  static char	  md5string[MD5_HASH_LENGTH + 1] = "";
  unsigned char   md5out[MD5_HASH_LENGTH + 1] = "";
  MD5_CTX ctx;

  MD5_Init(&ctx);
  MD5_Update(&ctx, string, strlen(string));
  MD5_Final(md5out, &ctx);
  strncpyz(md5string, btoh(md5out, MD5_DIGEST_LENGTH), sizeof(md5string));
  OPENSSL_cleanse(&ctx, sizeof(ctx));
  return md5string;
}

char *
MD5FILE(const char *bin)
{
  static char     md5string[MD5_HASH_LENGTH + 1] = "";
  unsigned char   md5out[MD5_HASH_LENGTH + 1] = "", buffer[1024] = "";
  MD5_CTX ctx;
  size_t binsize = 0, len = 0;
  FILE *f = NULL;

  if (!(f = fopen(bin, "rb")))
    return "";

  MD5_Init(&ctx);
  while ((len = fread(buffer, 1, sizeof buffer, f))) {
    binsize += len;
    MD5_Update(&ctx, buffer, len);
  }
  MD5_Final(md5out, &ctx);
  strncpyz(md5string, btoh(md5out, MD5_DIGEST_LENGTH), sizeof(md5string));
  OPENSSL_cleanse(&ctx, sizeof(ctx));

  return md5string;
}

char *SHA1(const char *string)
{
  static char	  sha1string[SHA_HASH_LENGTH + 1] = "";
  unsigned char   sha1out[SHA_HASH_LENGTH + 1] = "";
  SHA_CTX ctx;

  SHA1_Init(&ctx);
  SHA1_Update(&ctx, string, strlen(string));
  SHA1_Final(sha1out, &ctx);
  strncpyz(sha1string, btoh(sha1out, SHA_DIGEST_LENGTH), sizeof(sha1string));
  OPENSSL_cleanse(&ctx, sizeof(ctx));
  return sha1string;
}

/* convert binary hashes to hex */
char *btoh(const unsigned char *md, int len)
{
  int i;
  char buf[100] = "", *ret = NULL;

  for (i = 0; i < len; i++)
    sprintf(&(buf[i*2]), "%02x", md[i]);

  ret = buf;
  return ret;
}

