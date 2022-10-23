make all TARGET=intel64
cd ./SAS/
./run.sh
cd ..
rm ./pinatrace.out
../../../pin -t obj-intel64/pinatrace.so -- ./SAS/a.out
