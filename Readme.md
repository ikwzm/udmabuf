# udmabuf(User space mappable DMA Buffer)#


## はじめに##


### udmabufとは###


udmabuf はLinux のカーネル空間に連続したメモリ領域をDMAバッファとして確保し、ユーザー空間からmmapでアクセス可能にするためのデバイスドライバです。主にUIO(User space I/O)を使ってユーザー空間でデバイスドライバを動かす場合のDMAバッファを提供します。

ユーザー空間でudmabufを利用する際は、/dev/udmabuf0をopenしてmmapすると、ユーザー空間からDMAバッファにアクセスすることが出来ます。openする際にO_SYNCフラグをセットすることによりCPUキャッシュを無効にすることが出来ます。また、/sys/class/udmabuf/udmabuf0/phys_addr を読むことにより、DMAバッファの物理空間上のアドレスを知ることが出来ます。

udmabufのバッファの大きさやデバイスのマイナー番号は、デバイスドライバのロード時(insmodによるロードなど)に指定できます。またプラットフォームによってはデバイスツリーに記述しておくこともできます。


### 対応プラットフォーム###


* OS : Linux Kernel Version 3.6

* CPU: ARM(ZYNQ)


### 構成###


![構成](./udmabuf1.jpg)

## 使い方##


### コンパイル###


次のようなMakefileを用意しています。

```
obj-m : udmabuf.o   
all:   
	make -C /usr/src/kernel M=$(PWD) modules   
clean:   
	make -C /usr/src/kernel M=$(PWD) clean
```



### インストール###


insmod でudmabufのカーネルドライバをロードします。この際に引数を渡すことによりDMAバッファを確保してデバイスドライバを作成します。insmod の引数で作成できるDMAバッファはudmabuf0、udmabuf1、udmabuf2、udmabuf3の最大４つです。

```
zynq$ insmod udmabuf.ko udmabuf0=1048576
udmabuf udmabuf0: driver installed   
udmabuf udmabuf0: major number   = 248   
udmabuf udmabuf0: minor number   = 0   
udmabuf udmabuf0: phys address   = 0x1e900000   
udmabuf udmabuf0: buffer size    = 1048576
zynq$ ls -la /dev/udmabuf0   
crw------- 1 root root 248, 0 Dec  1 09:34 /dev/udmabuf0
```

アンインストールするに rmmod を使います。

```
zynq$ rmmod udmabuf   
udmabuf udmabuf0: driver uninstalled
```




### デバイスツリーによる設定###


udmabufはinsmod の引数でDMAバッファを用意する以外に、Linuxのカーネルが起動時に読み込むdevicetreeファイルによってDMAバッファを用意する方法があります。devicetreeファイルに次のようなエントリを追加しておけば、insmod でロードする際に自動的にDMAバッファを確保してデバイスドライバを作成します。

```devcetree.dts
		udmabuf0@devicetree {   
			compatible = "ikwzm,udmabuf-0.10.a";   
			minor-number = <0>;   
			size = <0x00100000>;   
		};   
```



sizeでDMAバッファの容量をバイト数で指定します。

minor-number でudmabufのマイナー番号を指定します。マイナー番号は0から31までつけることができます。ただし、insmodの引数の方が優先され、マイナー番号がかち合うとdevicetreeで指定した方が失敗します。


```
zynq$ insmod udmabuf.ko
udmabuf udmabuf0: driver installed   
udmabuf udmabuf0: major number   = 248   
udmabuf udmabuf0: minor number   = 0   
udmabuf udmabuf0: phys address   = 0x1e900000   
udmabuf udmabuf0: buffer size    = 1048576
zynq$ ls -la /dev/udmabuf0   
crw------- 1 root root 248, 0 Dec  1 09:34 /dev/udmabuf0
```



### デバイスファイル###


udmabufをinsmodでカーネルにロードすると、次のようなデバイスファイルが作成されます。

* /dev/udmabuf[0-31]

* /sys/class/udmabuf/udmabuf[0-31]/phys_addr

* /sys/class/udmabuf/udmabuf[0-31]/size

* /sys/class/udmabuf/udmabuf[0-31]/sync_mode



/dev/udmabuf[0-31]はmmapでユーザー空間にマッピングする際に使用します。

```C:udmabuf_test.c
	if ((fd  = open("/dev/udmabuf0", O_RDWR)) != -1) {   
		buf = mmap(NULL, buf_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)   
		/* ここでbufに読み書きする処理を行う */   
		close(fd);   
	}   
```


/sys/class/udmabuf/udmabuf[0-31]/phys_addr はDMAバッファの物理アドレスが読めます。

```C:udmabuf_test.c
	unsigned char  attr[1024];   
	unsigned long  phys_addr;   
	if ((fd  = open("/sys/class/udmabuf/udmabuf0/phys_addr", O_RDONLY)) != -1) {   
		read(fd, attr, 1024);   
		sscanf(attr, "%x", &phys_addr);   
		close(fd);   
	}   
```


/sys/class/udmabuf/udmabuf[0-31]/size はDMAバッファのサイズが読めます。

```C:udmabuf_test.c
	unsigned char  attr[1024];   
	unsigned int   buf_size;   
	if ((fd  = open("/sys/class/udmabuf/udmabuf0/size", O_RDONLY)) != -1) {   
		read(fd, attr, 1024);   
		sscanf(attr, "%d", &buf_size);   
		close(fd);   
	}   
```


/sys/class/udmabuf/udmabuf[0-31]/sync_mode はudmabufをopenする際にO_SYNCを指定した場合の動作を指定します。

```C:udmabuf_test.c
	unsigned char  attr[1024];   
	unsigned long  sync_mode = 2;   
	if ((fd  = open("/sys/class/udmabuf/udmabuf0/sync_mode", O_WRONLY)) != -1) {   
		sprintf(attr, "%d", sync_mode);   
		write(fd, attr, strlen(attr));   
		close(fd);   
	}
```

O_SYNCおよびキャッシュの設定に関しては次の節で説明します。




### DMAバッファとCPUキャッシュのコヒーレンシ###


CPUは通常キャッシュを通じてメインメモリ上のDMAバッファにアクセスしますが、アクセラレータは直接メインメモリ上のDMAバッファにアクセスします。その際、問題になるのはCPUのキャッシュとメインメモリとのコヒーレンシ(内容の一貫性)です。

ハードウェアでコヒーレンシを保証できる場合、CPUキャッシュを有効にしても問題はありません。例えばZYNQにはAGP(Accelerator Coherency Port)があり、アクセラレータ側がこのPortを通じてメインメモリにアクセスする場合は、ハードウェアによってCPUキャッシュとメインメモリとのコヒーレンシが保証できます。

ハードウェアでコヒーレンシを保証できない場合、別の方法でコヒーレンシを保証しなければなりません。udmabufでは単純にCPUがDMAバッファへのアクセスする際はCPUキャッシュを無効にすることでコヒーレンシを保証しています。CPUキャッシュを無効にする場合は、udmabufをopenする際にO_SYNCフラグを設定します。

```C:udmabuf_test.c
	/* CPUキャッシュを無効にする場合はO_SYNCをつけてopen する */   
	if ((fd  = open("/dev/udmabuf0", O_RDWR | O_SYNC)) != -1) {   
		buf = mmap(NULL, buf_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)   
		/* ここでbufに読み書きする処理を行う */   
		close(fd);   
	}   
```

O_SYNCフラグを設定した場合のキャッシュの振る舞いはsync_modeで設定します。sync_modeには次の値が設定できます。

* sync_mode=0:  常にCPUキャッシュが有効。つまりO_SYNCフラグの有無にかかわらず常にCPUキャッシュは有効になります。

* sync_mode=1: O_SYNCフラグが設定された場合、CPUキャッシュを無効にします。

* sync_mode=2: O_SYNCフラグが設定された場合、CPUがDMAバッファに書き込む際、ライトコンバインします。ライトコンバインとは、基本的にはCPUキャッシュは無効ですが、複数の書き込みをまとめて行うことで若干性能が向上します。

* sync_mode=3: O_SYNCフラグが設定された場合、DMAコヒーレンシモードにします。といっても、DMAコヒーレンシモードに関してはまだよく分かっていません。



参考までに、CPUキャッシュを有効/無効にした場合の次のようなプログラムを実行した際の処理時間を示します。

```C:udmabuf_test.c
int check_buf(unsigned char* buf, unsigned int size)   
{   
    int m = 256;   
    int n = 10;   
    int i, k;   
    int error_count = 0;   
    while(--n > 0) {   
      for(i = 0; i < size; i = i + m) {   
        m = (i+256 < size) ? 256 : (size-i);   
        for(k = 0; k < m; k++) {   
          buf[i+k] = (k & 0xFF);   
        }   
        for(k = 0; k < m; k++) {   
          if (buf[i+k] != (k & 0xFF)) {   
            error_count++;   
          }   
        }   
      }   
    }   
    return error_count;   
}   
```



<table border="2">
  <tr>
    <td align="center" rowspan="2">O_SYNC</td>
    <td align="center" rowspan="2">sync_mode</td>
    <td align="center" colspan="3">DMAバッファのサイズ</td>
  </tr>
  <tr>
    <td align="center">1MByte</td>
    <td align="center">5MByte</td>
    <td align="center">10MByte</td>
  </tr>
  <tr>
    <td>無</td>
    <td>-</td>
    <td align="right">0.437[sec]</td>
    <td align="right">2.171[sec]</td>
    <td align="right">4.375[sec]</td>
  </tr>
  <tr>
    <td rowspan="4">有</td>
    <td>0</td>
    <td align="right">0.434[sec]</td>
    <td align="right">2.169[sec]</td>
    <td align="right">4.338[sec]</td>
  </tr>
  <tr>
    <td>1</td>
    <td align="right">2.283[sec]</td>
    <td align="right">11.414[sec]</td>
    <td align="right">22.830[sec]</td>
  </tr>
  <tr>
    <td>2</td>
    <td align="right">2.284[sec]</td>
    <td align="right">11.418[sec]</td>
    <td align="right">22.826[sec]</td>
  </tr>
  <tr>
    <td>3</td>
    <td align="right">2.282[sec]</td>
    <td align="right">11.409[sec]</td>
    <td align="right">22.827[sec]</td>
  </tr>
</table>


