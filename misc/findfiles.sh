#!/bin/sh

rm -rf tmp tmp2


FILES=""
misc/grep -r "$@" * > tmp
sed -e 's/\(.*\):.*/\1/' tmp > tmp2
uniq tmp2 > tmp
rm -f tmp2
for f in `cat tmp`
do
  FILES="${FILES} ${f}"
done
rm -rf tmp
echo ${FILES}
