#! /bin/sh

path="${HOME}/wraith/misc/cron_cvs_build.sh"
#path="${PWD}/`basename $0`"

if test $# -lt 1; then
 #Crontab this script.
 HOURLY=10
 TMPFILE=`mktemp /tmp/cron.XXXXXX` || exit 1
 crontab -l | grep -v "${path}" > ${TMPFILE}
 echo "* 0 * * * ${path} 1 > /dev/null 2>&1" >> ${TMPFILE}
 crontab ${TMPFILE}
 rm ${TMPFILE}
else
 cd ${HOME}/wraith
 cvs update
 ./build -C -n -s lordares@endurance.quadspeedi.net:public_html/nightly/ all
fi
