# MagicPlxel
由于本项目纯属自娱自乐，还存在很多问题和缺陷，**资料仅供参考**。
## 关于3D打印光栅：
正面的缺口可以更大，容下更大的亚克力板，这样四周的积木就可以直接压住亚克力板。每边增加3mm即可。
## 关于PCB：
原理图中已经给出要做的修改，复位引脚和地之间加一个1uF的电容，GPIO12不能用作锂电池充电检测引脚。  
除此之外，PCB的形状是不合适的，我把L形状画反了，可以根据需求做修改。
## 关于程序：
基于esp-adf 4.1。  
程序存在的缺陷：  
1.天气代码中，因为我在的地区不下雪，所以我就没画对应的图标，文件名<led_ui.c>。  
2.连接WIFI时，先使用宏定义的SSID和password进行尝试1（可修改）次，如不成功，进入smartconfig模式，详情参考乐鑫的手册。  
3.由于我第一次使用rtos，所以代码很乱，所有app都在一个文件里。  
程序存在的bug：  
1.运行时，有一定概率复位。  
2.关闭蓝牙时会崩溃，我猜是恢复了被挂起的天气tcp任务，因为当蓝牙和天气tcp任务同时存在就会复位，还有待优化。  
可能还有没被发现的bug......  
  
项目演示：https://www.bilibili.com/video/BV1Xv41167Di/

------------------------------------------------------------------------------------------  
#2021-4-16 更新  
有朋友反映不显示频谱，我仔细看了下，是有个文件忘记上传了
增加bluetooth_service.c文件，  
请将该文件放置到esp-adf/components/bluetooth_service目录下并替代原文件
