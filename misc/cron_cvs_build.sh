#! /bin/sh

path="${HOME}/cron_cvs_build.sh"
#path="${PWD}/`basename $0`"
PRIVATE_ROOT=":pserver:changelog:S0meP4ass:@box.shatow.net:/cvs"

if test $# -lt 1; then
 #Crontab this script.
 TMPFILE=`mktemp /tmp/cron.XXXXXX` || exit 1
 crontab -l | grep -v "${path}" > ${TMPFILE}
 echo "* 0 * * * ${path} 1 > /dev/null 2>&1" >> ${TMPFILE}
 crontab ${TMPFILE}
 rm ${TMPFILE}
 
 exit 0
fi

cd ${HOME}
mkdir tmp
cd ${HOME}/tmp/
cvs -d $PRIVATE_ROOT login
cvs -z9 -d $PRIVATE_ROOT checkout wraith
cd ${HOME}/tmp/wraith/
# export PWD="${HOME}/wraith/"
./build -C -n -s lordares@endurance.quadspeedi.net:public_html/nightly/ all
cd ${HOME}/tmp/
cp ${HOME}/tmp/wraith/misc/cron_cvs_build.sh ${HOME}/tmp/cron_cvs_build.sh
rm -rf ${HOME}/tmp/wraith
cvs -d $PRIVATE_ROOT logout

mv ${HOME}/tmp/cron_cvs_build.sh ${HOME}/cron_cvs_build.sh


