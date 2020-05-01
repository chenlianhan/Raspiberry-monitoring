### 文件结构及用途
用于解释该Project的功能

---

#### 文件结构树
```
.
├── compile_c.sh
├── send_cam
├── send_cam.cpp
└── send_video.sh
```
compile_c.sh   ：  编译的脚本文件
send_cam       ：  可运行文件
send_cam.cpp   ：  主要cpp文件
send_video.sh  :   发送脚本【运行脚本】

---

#### 使用方法
```
./send_cam 设备尾号 分辨率 编码速率
设备尾号 : 0 , 1 , 2 , 3 ......
分辨率   ：320x240 , 640x480 , 1280x720 .......
编码速率 : low , fast , veryfast , superfast , ultrafast ........
```
例如：
```
./send_cam 0 640x480 superfast
```
或者直接运行send_video.sh脚本文件可以直接发送
