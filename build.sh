#! /bin/sh
# We want to use BASH, not whatever /bin/sh points to.
if test -z "$BASH"; then
  PATH=/usr/local/sbin:/usr/local/bin:/sbin:/bin:/usr/sbin:/usr/bin
  bash="`which bash`"
  ${bash} $0 ${1+"$@"}
  exit 0
fi


PATH=/usr/local/sbin:/usr/local/bin:/sbin:/bin:/usr/sbin:/usr/bin:${HOME}/bin
# Awk out the version from source
AWK="`which awk`"
if test -z "${AWK}"; then
 AWK="`which gawk`"
fi

ver="?.?.?"
ver=`grep "char" src/main.c | $AWK '/egg_version/ {print $5}' | sed -e 's/\"//g' | sed -e 's/\;//g'`
echo $ver | grep "devel" > /dev/null 2>&1
if [ $? -eq 0 ]; then
  devel="1"
else
  devel="0"
fi
rm -f ts ts.exe
gcc -o ts src/timestamp.c > /dev/null 2>&1
if [ -d .git ]; then
  BUILDTS=$(git log -1 --pretty=format:%ct HEAD)
else
  BUILDTS=`grep -m 1 "BUILDTS = " Makefile.in | ${AWK} '{print $3}'` 
fi
builddate=`./ts ${BUILDTS}`
rm -f ts ts.exe
clear
#Display a banner
#bt=`grep -A 5 "char \*wbanner(void) {" src/misc.c | grep "switch" | sed -e "s/.*switch (randint(\(.\))) {/\1/"`
#bn=$[($RANDOM % ${bt})]
bn=0
banner=`grep -A 15 "char \*wbanner(void) {" src/misc.c | grep "case ${bn}:" | sed -e "s/.*case ${bn}: return STR(\"\(.*\)\");/\1/"`
echo -e "${banner}"
echo -e "Version:   ${ver}\nBuild:     ${builddate}"

usage() 
{
    echo
    echo "Usage: $0 [-bcCdnP] [-q FILE]"
    echo
    echo "    The options are as follows:"
    echo "    -b        Use bzip2 instead of gzip when packaging."
    echo "    -c        Cleans up old binaries/files before compile."
    echo "    -C        Preforms a distclean before making."
    echo "    -d        Builds a debug package."
    echo "    -n        Do not package the binaries."
    echo "    -q  FILE  What file to use as the cfg file."
    echo "    -P        For development (Don't compile/rm binaries)"
    echo "    -s        scp target."
}

debug=0
clean=0
nopkg=0
bzip=0
pkg=0
scp=0
nightly=0
pack=pack/pack.cfg

while getopts bCcdhnNPq:s: opt ; do
        case "$opt" in
        b) bzip=1 ;;
	c) clean=1 ;;
	C) clean=2 ;;
        d) debug=1 ;;
	h) usage; exit 0 ;;
        n) nopkg=1 ;;
	N) nightly=1 ;;
	q) pack="$OPTARG" ;;
        P) pkg=1 ;;
	s) scp="$OPTARG" ;;
        *) usage; exit 1 ;;

        esac
done

shift $(($OPTIND - 1))

PACKNAME=`grep "PACKNAME " ${pack} | $AWK '/PACKNAME/ {print $2}'`
echo "Packname:  ${PACKNAME} (${pack})"
echo -e `grep -m 1 "http://" src/misc.c | sed -e "s/.*\- \(.*\) \-.*/\1/"`
echo

if test -z "$1"; then
 usage
 exit 1
fi

rm=1
compile=1
if [ $pkg = "1" ]; then
 rm=0
 compile=0
fi

if [ $debug = "1" ]; then
 dmake="d"
 d="-debug"
else
 dmake=""
 d=""
fi

# Figure what bins we're making
case `uname` in
  Linux) os=Linux;;
  FreeBSD) os=FreeBSD;;
#  FreeBSD) case `uname -r` in
#    5*) os=FreeBSD5;;
#    4*) os=FreeBSD4;;
#    3*) os=FreeBSD3;;
#  esac;;
  OpenBSD) os=OpenBSD;;
  NetBSD) os=NetBSD;;
  SunOS) os=Solaris;;
  CYGWIN*) os=Cygwin; extras="/bin/cygwin1.dll"; exe=".exe";;
esac

if test -z $os
then
  echo "[!] Automated packaging disabled, `uname` isn't recognized"
fi

if [ $nightly = "1" ]; then
  chmod 0700 private/id_dsa
  remotedate="`ssh -q -i private/id_dsa bdrewery@endurance.quadspeedi.net ls -al public_html/nightly/${os}-stable.tar.gz | \
             sed -e 's/.*\-> \([0-1][0-2]\.[0-3][0-9]\.[0-9][0-9][0-9][0-9]\)\-.*/\1/'`"
  if [ "${remotedate}" = "${builddate}" ]; then
    exit 0
  fi
fi

if [ $compile = "1" ]; then

 echo "[*] Building ${PACKNAME} for $os"

 MAKE="`which gmake`"
 if test -z "${MAKE}"; then
  MAKE="`which make`"
 fi

 if [ $clean = "2" ]; then
  if test -f Makefile; then
   echo "[*] DistCleaning old files..."
   ${MAKE} distclean > /dev/null
  fi
 fi

 # Run ./configure, then verify it's ok
 echo "[*] Configuring..."
 umask 077 >/dev/null

 ./configure --silent

 if [ $clean = "1" ]; then
  echo "[*] Cleaning up old binaries/files..."
  ${MAKE} clean > /dev/null
 fi
fi

_build()
{
 if [ $compile = "1" ]; then
  echo "[*] Building ${dmake}${tb}..."
  ${MAKE} ${dmake}${tb}
  if ! test -f ${tb}; then
    echo "[!] ${dmake}${tb} build failed"
    exit 1
  fi
 fi
 if [ $nopkg = "0" -o $pkg = "1" ]; then
  echo "[*] Hashing and initializing settings in binary"
   cp ${tb} ${tb}.$os-$ver${d} > /dev/null 2>&1
  ./${tb}.$os-$ver${d} -q ${pack}
  rm=1
 elif [ $nopkg = "0" ]; then
   mv ${tb} ${tb}.$os-$ver${d} > /dev/null 2>&1
 fi
}

tb="wraith"
_build

if [ $bzip = "1" ]; then
  zip="j"
  ext="bz2"
else
  zip="z"
  ext="gz"
fi

# Wrap it nicely up into an archive
if [ $nopkg = "0" -o $pkg = "1" ]; then
  echo "[*] Packaging..."
  #This MALLOC business is to not Abort 'tar' due to some bug it has.
  tmp=${MALLOC_CHECK_}
  unset MALLOC_CHECK_
  tar -c${zip}f ${PACKNAME}.$os-$ver${d}.tar.${ext} wraith.$os-$ver${d}
  chmod 0644 ${PACKNAME}.$os-$ver${d}.tar.${ext}
  if test -n "$tmp"; then
    declare -x MALLOC_CHECK_=$tmp
  fi
  if [ $rm = "1" ]; then
    rm -f *$os-$ver${d}
  fi
  echo "Binaries are now in '${PACKNAME}.$os-$ver${d}.tar.${ext}'."
 
  if ! [ $scp = 0 ]; then
    chmod 0700 private/id_dsa
    scp -i private/id_dsa -B -p -C ${PACKNAME}.$os-$ver${d}.tar.${ext} $scp

    scp_target=`echo $scp | cut -d : -f 1`
    dir=`echo $scp | cut -d : -f 2`
    if [ "$devel" = "1" ]; then
      ssh -i private/id_dsa $scp_target ln -fs ${PACKNAME}.$os-$ver${d}.tar.${ext} ${dir}/${PACKNAME}.${os}-devel.tar.gz
    else
      ssh -i private/id_dsa $scp_target ln -fs ${PACKNAME}.$os-$ver${d}.tar.${ext} ${dir}/${PACKNAME}.${os}-stable.tar.gz
      ssh -i private/id_dsa $scp_target ln -fs ${PACKNAME}.$os-$ver${d}.tar.${ext} ${dir}/${PACKNAME}.${os}-base.tar.gz
    fi

  fi
elif ! [ $scp = "0" ]; then
  cp wraith wraith.$os-$ver${d} > /dev/null 2>&1
  cp ChangeLog ChangeLog-${builddate}
  gzip -9 ChangeLog-${builddate}
  #This MALLOC business is to not Abort 'tar' due to some bug it has.
  tmp=${MALLOC_CHECK_}
  unset MALLOC_CHECK_

  cp ${extras} .
  for file in $extras
  do
    extras_local="${extras_local} `basename $file`"
  done

  tar -c${zip}f ${builddate}-$os-$ver${d}.tar.${ext} wraith.$os-$ver${d}${exe} ChangeLog-${builddate}.gz ${extras_local}
  chmod 0644 ${builddate}-$os-$ver${d}.tar.${ext}
  if test -n "$tmp"; then
    declare -x MALLOC_CHECK_=$tmp
  fi
  chmod 0700 private/id_dsa
  scp -i private/id_dsa -B -p -C ${builddate}-$os-$ver${d}.tar.${ext} $scp
  rm -rf ${builddate}-$os-$ver${d}.tar.${ext} wraith.$os-$ver${d}${exe} ChangeLog-${builddate}.gz ${extras_local}
  if [ "$devel" = "1" ]; then
    ssh -i private/id_dsa shatow@wraith.shatow.net ln -fs ${builddate}-$os-$ver${d}.tar.${ext} public_html/nightly/${os}-devel.tar.gz
  else
    ssh -i private/id_dsa shatow@wraith.shatow.net ln -fs ${builddate}-$os-$ver${d}.tar.${ext} public_html/nightly/${os}-stable.tar.gz
    ssh -i private/id_dsa shatow@wraith.shatow.net ln -fs ${builddate}-$os-$ver${d}.tar.${ext} public_html/nightly/${os}.tar.gz
  fi
fi


exit 0
