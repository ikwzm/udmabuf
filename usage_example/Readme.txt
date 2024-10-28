This readme is just for our internal usage scenarios. 

A far more detailed readme is available in the repositories
https://github.com/ikwzm/udmabuf
https://github.com/tsisw/udmabuf 

===============================================================
Compiling (in aws arm setup):

To compile (compiles against the running kernel version and header files): 

(cd to udmabuf directory)
make clean
make all

To clean:   

(cd to udmabuf directory)
make clean


===============================================================
Cross compiling (For FPGA setup):

[Needs the cross compilation related env setup and the target kernel source.]

[cross compilation env variables]

(cd to top folder)

export TOP_FOLDER=`pwd`
cd $TOP_FOLDER
echo "Current working dir (TOP_FOLDER): $TOP_FOLDER"
export PATH=/proj/local/gcc-arm-11.2-2022.02-x86_64-aarch64-none-linux-gnu/bin:$PATH
export ARCH=arm64
export CROSS_COMPILE=aarch64-none-linux-gnu-
set -e


[Target kernel source:
Out of tree module compilation requires the Module.symvers file in the target kernel directory. 
So make sure a 'make modules' command is executed as part of linux kernel compilation.]
 
(cd to top folder)

cd $TOP_FOLDER
if [ -e linux-socfpga ]
then
echo "linux-socfpga exists"
cd linux-socfpga
export KERNEL_SRC=`pwd`
echo "KERNEL_SRC: $KERNEL_SRC"
cd $TOP_FOLDER
else
echo "linux-socfpga does not exist"
fi


[module compilation]

if [ -e contig_mem ]
then
echo "contig_mem exists"    
cd contig_mem/udmabuf
make clean
make all
else
mkdir contig_mem
cd contig_mem
cp -r /proj/work/dmohapatra/contig_mem/udmabuf ./
cd udmabuf
make clean
make all
fi

===============================================================
Inserting the module:



===============================================================
Relevant module parameters:
The parameters most relevant for us are the size parameters. 
