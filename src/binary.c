/*
 * binary.c -- handles:
 *   misc update functions
 *   md5 hash verifying
 *
 * $Id$
 */

#include "common.h"
#include "binary.h"
#include "crypt.h"
#include "shell.h"
#include "misc.h"
#include "main.h"
#include "salt.h"
#include "misc_file.h"


#define PREFIXLEN 16

/*
typedef struct encdata_struct {
  char prefix[PREFIXLEN];
  char data[65];
} encdata_t;

static encdata_t encdata = {
  "AAAAAAAAAAAAAAAA",
  ""
};
*/
typedef struct bindata_struct {
  char prefix[PREFIXLEN];
  char hash[65];
  char packname[65];
  char shellhash[65];
  char bdhash[65];
  char owners[1024];
  char hubs[1024];
  char owneremail[1024];
  char salt1[65];
  char salt2[45];
  char dccprefix[25];
  char pad_3418_to_3488[5];
} bindata_t;

static bindata_t bindata = {
  "AAAAAAAAAAAAAAAA",
  "", "", "", "", "", "", "", "", "", "", "",
};

#define PACK_ENC 1
#define PACK_DEC 2
static void edpack(struct bindata_struct *, const char *, int);

int checked_bin_buf = 0;

static char *
bin_md5(const char *fname, int todo, MD5_CTX * ctx)
{
  static char hash[MD5_HASH_LENGTH + 1] = "";
  unsigned char md5out[MD5_HASH_LENGTH + 1] = "", buf[17] = "";
  FILE *f = NULL;
  size_t len = 0;

  checked_bin_buf++;
  if (!(f = fopen(fname, "rb")))
    werr(ERR_BINSTAT);

  while ((len = fread(buf, 1, sizeof buf - 1, f))) {
    if (!memcmp(buf, &bindata.prefix, PREFIXLEN)) {
      break;
    }
    MD5_Update(ctx, buf, len);
  }

  fclose(f);
  MD5_Final(md5out, ctx);
  strncpyz(hash, btoh(md5out, MD5_DIGEST_LENGTH), sizeof(hash));
  OPENSSL_cleanse(&ctx, sizeof(ctx));

  if (todo == WRITE_MD5) {
    char *fname_bak = NULL, s[DIRMAX] = "";
    FILE *fn = NULL;            /* the new binary */
    int i = 0, fd = -1;
    size_t size = 0;


    size = strlen(fname) + 2;
    fname_bak = calloc(1, size);
    egg_snprintf(fname_bak, size, "%s~", fname);
    size = 0;

    if (!(f = fopen(fname, "rb")))
      werr(ERR_BINSTAT);

    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);

    egg_snprintf(s, sizeof s, ".bin-XXXXXX");
    if ((fd = mkstemp(s)) == -1 || (fn = fdopen(fd, "wb")) == NULL) {
      if (fd != -1) {
        unlink(s);
        close(fd);
      }
      fatal("Can't create temporary file!", 0);
    }

    while ((len = fread(buf, 1, sizeof(buf) - 1, f))) {
      if (i) {                  /* to skip bytes for hash */
        i -= sizeof(buf) - 1;
        continue;
      }
      if (fwrite(buf, sizeof(buf) - 1, 1, fn) != 1) {
        fclose(f);
        fclose(fn);
        unlink(s);
        werr(ERR_BINSTAT);
      }
/*
      if (!memcmp(buf, &encdata.prefix, PREFIXLEN)) {
        // now we have 65 for data :D
        char *enc_hash = NULL;

        enc_hash = encrypt_string(SALT1, hash);
        fwrite(enc_hash, strlen(enc_hash), 1, fn);
        i = strlen(enc_hash);   // skip the next strlen(enc_hash) bytes
        free(enc_hash);
      }
*/
      if (!memcmp(buf, &bindata.prefix, PREFIXLEN)) {
        strncpyz(bindata.hash, hash, 65);
        edpack(&bindata, hash, PACK_ENC);
        fwrite(&bindata.hash, sizeof(struct bindata_struct) - PREFIXLEN, 1, fn);
        i = sizeof(struct bindata_struct) - PREFIXLEN;
      }
    }

    fclose(f);
    fclose(fn);

    if (movefile(fname, fname_bak))
      fatal("Crappy os :D", 0);

    if (movefile(s, fname))
      fatal("Crappy os :D", 0);

    fixmod(fname);
    unlink(fname_bak);
    unlink(s);
  }
  return hash;
}

static int
readcfg(const char *cfgfile)
{
  FILE *f = NULL;
  char *buffer = NULL, *p = NULL;
  int skip = 0, line = 0;

  f = fopen(cfgfile, "r");
  if (!f) {
    printf("Error: Can't open '\%s' for reading\n", cfgfile);
    exit(1);
  }
  printf("Reading '\%s' ", cfgfile);
  while ((!feof(f)) && ((buffer = step_thru_file(f)) != NULL)) {
    line++;
    if ((*buffer)) {
      if (strchr(buffer, '\n'))
        *(char *) strchr(buffer, '\n') = 0;
      if ((skipline(buffer, &skip)))
        continue;
      if (strchr(buffer, '<') || strchr(buffer, '>')) {
        printf(" Failed\n");
        printf("%s:%d: error: Look at your configuration file again...\n", cfgfile, line);
        exit(1);
      }
      p = strchr(buffer, ' ');
      while (p && (strchr(LISTSEPERATORS, p[0])))
        *p++ = 0;
      if (p) {
        if (!egg_strcasecmp(buffer, "packname")) {
          strncpyz(bindata.packname, trim(p), sizeof bindata.packname);
          printf(".");
        } else if (!egg_strcasecmp(buffer, "shellhash")) {
          strncpyz(bindata.shellhash, trim(p), sizeof bindata.shellhash);
          printf(".");
        } else if (!egg_strcasecmp(buffer, "bdhash")) {
          strncpyz(bindata.bdhash, trim(p), sizeof bindata.bdhash);
          printf(".");
        } else if (!egg_strcasecmp(buffer, "dccprefix")) {
          strncpyz(bindata.dccprefix, trim(p), sizeof bindata.dccprefix);
          printf(".");
        } else if (!egg_strcasecmp(buffer, "owner")) {
          strcat(bindata.owners, trim(p));
          strcat(bindata.owners, ",");
          printf(".");
        } else if (!egg_strcasecmp(buffer, "owneremail")) {
          strcat(bindata.owneremail, trim(p));
          strcat(bindata.owneremail, ",");
          printf(".");
        } else if (!egg_strcasecmp(buffer, "hub")) {
          strcat(bindata.hubs, trim(p));
          strcat(bindata.hubs, ",");
          printf(".");
        } else if (!egg_strcasecmp(buffer, "salt1")) {
          strcat(bindata.salt1, trim(p));
          printf(".");
        } else if (!egg_strcasecmp(buffer, "salt2")) {
          strcat(bindata.salt2, trim(p));
          printf(".");
        } else {
          printf("%s %s\n", buffer, p);
          printf(",");
        }
      }
    }
    buffer = NULL;
  }
  if (f)
    fclose(f);
  if (!bindata.salt1[0] || !bindata.salt2[0]) {
    /* Write salts back to the cfgfile */
    char salt1[SALT1LEN + 1] = "", salt2[SALT2LEN + 1] = "";

    printf("Creating Salts");
    if ((f = fopen(cfgfile, "a")) == NULL) {
      printf("Cannot open cfgfile for appending.. aborting\n");
      exit(1);
    }
    make_rand_str(salt1, SALT1LEN);
    make_rand_str(salt2, SALT2LEN);
    salt1[sizeof salt1] = salt2[sizeof salt2] = 0;
    fprintf(f, "SALT1 %s\n", salt1);
    fprintf(f, "SALT2 %s\n", salt2);
    fflush(f);
    fclose(f);
  }
  printf(" Success\n");
  return 1;
}

static void edpack(struct bindata_struct *incfg, const char *hash, int what)
{
  char *tmp = NULL;
  char *(*enc_dec_string)();
  
  if (what == PACK_ENC)
    enc_dec_string = encrypt_string;
  else
    enc_dec_string = decrypt_string;

  tmp = enc_dec_string(hash, incfg->hash);
  egg_snprintf(incfg->hash, sizeof(incfg->hash), tmp);
  free(tmp);

  tmp = enc_dec_string(hash, incfg->packname);
  egg_snprintf(incfg->packname, sizeof(incfg->packname), tmp);
  free(tmp);

  tmp = enc_dec_string(hash, incfg->shellhash);
  egg_snprintf(incfg->shellhash, sizeof(incfg->shellhash), tmp);
  free(tmp);

  tmp = enc_dec_string(hash, incfg->bdhash);
  egg_snprintf(incfg->bdhash, sizeof(incfg->bdhash), tmp);
  free(tmp);

  tmp = enc_dec_string(hash, incfg->dccprefix);
  egg_snprintf(incfg->dccprefix, sizeof(incfg->dccprefix), tmp);
  free(tmp);

  tmp = enc_dec_string(hash, incfg->owners);
  egg_snprintf(incfg->owners, sizeof(incfg->owners), tmp);
  free(tmp);

  tmp = enc_dec_string(hash, incfg->owneremail);
  egg_snprintf(incfg->owneremail, sizeof(incfg->owneremail), tmp);
  free(tmp);

  tmp = enc_dec_string(hash, incfg->hubs);
  egg_snprintf(incfg->hubs, sizeof(incfg->hubs), tmp);
  free(tmp);
}


static void
tellconfig(struct bindata_struct *incfg)
{
  printf("hash: %s\n", incfg->hash);
  printf("packname: %s\n", incfg->packname);
  printf("shellhash: %s\n", incfg->shellhash);
  printf("bdhash: %s\n", incfg->bdhash);
  printf("dccprefix: %s\n", incfg->dccprefix);
  printf("owners: %s\n", incfg->owners);
  printf("owneremails: %s\n", incfg->owneremail);
  printf("hubs: %s\n", incfg->hubs);
}

static void
md5cfg(struct bindata_struct *incfg, MD5_CTX * ctx)
{
  MD5_Update(ctx, incfg->packname, strlen(incfg->packname));
  MD5_Update(ctx, incfg->shellhash, strlen(incfg->shellhash));
  MD5_Update(ctx, incfg->bdhash, strlen(incfg->bdhash));
  MD5_Update(ctx, incfg->dccprefix, strlen(incfg->dccprefix));
  MD5_Update(ctx, incfg->owners, strlen(incfg->owners));
  MD5_Update(ctx, incfg->owneremail, strlen(incfg->owneremail));
  MD5_Update(ctx, incfg->hubs, strlen(incfg->hubs));
}

void
check_sum(const char *fname, const char *cfgfile)
{
  MD5_CTX ctx;

  MD5_Init(&ctx);

  if (!bindata.hash[0]) {
    if (cfgfile) {
      printf("* CFGFILE: %s\n", cfgfile);
      readcfg(cfgfile);
    }
    printf("* Wrote checksum to binary. (%s)\n", bin_md5(fname, WRITE_MD5, &ctx));
    tellconfig(&bindata);
  } else {
    char *hash = NULL;


    hash = bin_md5(fname, GET_MD5, &ctx);

tellconfig(&bindata);
    edpack(&bindata, hash, PACK_DEC);
tellconfig(&bindata);

    if (strcmp(bindata.hash, hash)) {
      unlink(fname);
      fatal("!! Invalid binary", 0);
    }
  }
}
