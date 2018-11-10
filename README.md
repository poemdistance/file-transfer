# 此次更新相关事项：
* 修复了server端多次数据发送中可能导致发送失败的逻辑错误
* 添加了链接文件跟随 & 非跟随传输功能, 默认不跟随传输，如果需要跟随传输, 请添加-f参数
* 用switch改写了文件状态检测fstatus函数, 并去除了存在bug和无意义的rename功能
* 优化部分代码，并增加了代码修改的便捷性，可直接修改发送缓冲BUFLEN的大小而不必修改其他部分

# 程序功能:

* 在局域网中的设备间进行文件传输，支持单个文件和和整个文件夹的传输，比如连接同一个wifi的电脑和电脑，手  
机和手机之间，电脑和手机之间。(也可以电脑或手机开wifi热点，另一台设备连接之后也是局域网，可以相互传输  
文件)。其中server端用于发送文件，client端负责接收文件  

>注：Android手机要运行此程序的话，需要下载Termux，并安装gcc用于编译程序，然后运行即可，为了输入方便，可以再安装Hacker's Keyboard

# 用法：

* 编译：  
  * _$ gcc server.c -o server_  
  * _$ gcc client.c -o client_

* 运行:  
  * server端先运行：_$ ./server_  
  * client端后运行：_$ ./client hostname_

>注：hostname填写server端的ip地址,可以通过ifconfig命令查找,inet后面的那个点分十进制就是,形如：192.168.1.101即为ip地址

# 实例

下面以server端ip为192.168.1.101的为例  

* server端(在其中一个终端界面上运行):   _$ ./server_  

* client端(在另一个终端界面上运行):   _$ ./client 192.168.1.101_  

* 接着server端会接收到client端的连接请求,连接后需要在server端输入需要传  
输的文件的绝对路径,输入完成后client端会首先收到server端发送过来的文件名,  
然后进行文件名的检测，看当前文件夹下是否有同名文件会被覆盖，若有可以选择  
退出，覆盖，或重命名(运行时直接跟随提示操作即可)

>注：本机测试的话将hostname 改成127.0.0.1即可  


**点击图片观看演示视频**

[![IMAGE ALT TEXT HERE](https://img.youtube.com/vi/aPkljt47N_s/0.jpg)](https://www.youtube.com/watch?v=aPkljt47N_s)

# Related matters for this update:
* Fixed a logic error that may cause a send failure in multiple data transmissions on the server side.
* Added link file follow & non-following transfer function, default does not follow transfer, if you need to follow transfer, please add '-f' parameter
* Rewritten the file state detection 'fstatus' function with switch, and removed the wrong and meaningless rename function
* Optimize part of the code, and increase the convenience of code modification, you can directly modify the size of the send buffer 'BUFLEN' without having to modify other parts

# Program Function: 
* Transfer files between devices under the local area network. support single file and the whole folder transfer.  
For expamle, we can transfer files between computers or between smart phones which connect in a same wifi, also  
between smart phone and computer is ok.(we can also turn on our Data hotspot on our smart phones or computers,  
and other devices connect to the hotspot, it can also transfer files in between). The server is responsible for  
sending data, and the client is converse.  

>Tip: We have to download Termux and gcc to compile and run the program if want to transfer files between devices  
that contain android smart phone, it's more convenient to input to use Hacker's Keyboard, so we can download and  
install this app in our app store  

# Usage:  
* compile:  
  * _$ gcc server.c -o server_  
  * _$ gcc client.c -o client_  

* run:  
  * server side run firstly：_$ ./server_  
  * client side run secondary：_$ ./client hostname_

>Tip：hostname is ip address of server,we can find it by running the command line 'ifconfig' in our terminal.  
Just like：192.168.1.101(after inet)  

# Example:  
>The following takes the example of server ip being 192.168.1.101:

* server side(run in one terminal window): _$ ./server_

* client side(run in another terminal window): _$ ./client 192.168.1.101_

* Then the server side will receive the client side connection request. After the connection, it needs to input the  
absolute path of the file to be transmitted on the server side. After the input is completed, the client side will  
first receive the file name sent by the server side, and then detect the file name. See if there is a file with the  
same name under the current folder will be overwritten, if so, we can choose to exit, overwrite, or rename (Just  
follow the prompts to finish all things is ok :)  

>Tip: if you want to test the program by local computer please repalce the 'hostname' with 127.0.0.1
