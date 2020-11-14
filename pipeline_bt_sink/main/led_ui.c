#include "led_ui.h"
#include "led_strip.h"
#include <string.h>
#include "esp_log.h"


#define FALL_TIME 15

static const char *TAG_ui = "LED_UI:";
/*数字图案的取模方式：取8行,不包括星期
    ↓ ↓ ↓
□ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □|□ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □
□ □ ■ ■ ■ □ □ ■ □ □ □ □ ■ ■ ■ □ ■ ■ ■ □ □ □ ■ □ ■ □ ■ ■ ■ □ □ □|■ ■ ■ □ ■ ■ ■ □ ■ ■ ■ □ ■ ■ ■ □ □ □ ■ □ ■ □ ■ ■ □ □ □
□ □ ■ □ ■ □ ■ ■ □ □ ■ □ □ □ ■ □ □ □ ■ □ ■ □ ■ □ ■ □ ■ □ □ □ □ □|■ □ □ □ □ □ ■ □ ■ □ ■ □ ■ □ ■ □ □ ■ □ □ □ ■ □ □ ■ □ □
□ □ ■ □ ■ □ □ ■ □ □ □ □ ■ ■ ■ □ ■ ■ ■ □ □ □ ■ ■ ■ □ ■ ■ ■ □ □ □|■ ■ ■ □ □ □ ■ □ ■ ■ ■ □ ■ ■ ■ □ □ ■ □ □ □ ■ □ □ □ □ □
□ □ ■ □ ■ □ □ ■ □ □ ■ □ ■ □ □ □ □ □ ■ □ ■ □ □ □ ■ □ □ □ ■ □ □ □|■ □ ■ □ □ □ ■ □ ■ □ ■ □ □ □ ■ □ □ ■ □ □ □ ■ □ □ ■ □ □
□ □ ■ ■ ■ □ ■ ■ ■ □ □ □ ■ ■ ■ □ ■ ■ ■ □ □ □ □ □ ■ □ ■ ■ ■ □ □ □|■ ■ ■ □ □ □ ■ □ ■ ■ ■ □ ■ ■ ■ □ ■ □ □ □ □ □ ■ ■ □ □ □
□ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □|□ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □
□ □ ■ ■ ■ □ ■ ■ ■ □ ■ ■ ■ □ ■ ■ ■ □ ■ ■ ■ □ ■ ■ ■ □ ■ ■ ■ □ □ □|□ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □ □
*/
/*星期的图案提取方式
    23     ,24     ,39     ,
    23+4n*8,24+4n*8,39+4n*8,
    ...
    ...
*/
const uint8_t led_num[12][3] = {
    {0x3e, 0x22, 0x3e},     //0
    {0x24, 0x3e, 0x20},     //1
    {0x3a, 0x2a, 0x2e},     //2
    {0x2a, 0x2a, 0x3e},     //3
    {0x0e, 0x08, 0x3e},     //4
    {0x2e, 0x2a, 0x3a},     //5
    {0x3e, 0x2a, 0x3a},     //6
    {0x02, 0x02, 0x3e},     //7
    {0x3e, 0x2a, 0x3e},     //8
    {0x2e, 0x2a, 0x3e},     //9
    {0x14, 0x00, 0x00},     //:
    {0x20, 0x1c, 0x02},     ///
};
const uint8_t led_centi[5] = {0x02, 0x1c, 0x22, 0x22, 0x14};//摄氏度
/*
const uint8_t led_num[3*10] = {
    0x7c,0x22,0x7c,     //0
    0x24,0x3e,0x04,     //1
    0x5c,0x2a,0x74,     //2
    0x54,0x2a,0x7c,     //3
    0x70,0x08,0x7c,     //4
    0x74,0x2a,0x5c,     //5
    0x7c,0x2a,0x5c,     //6
    0x40,0x02,0x7c,     //7
    0x7c,0x2a,0x7c,     //8
    0x74,0x2a,0x7c      //9
    };
*/
/*
天气的显示：
  ↓ ↓ ↓...
□ □ □ □ □ □ □ □
□ ■ ■ ■ ■ ■ ■ □
□ ■ ■ ■ ■ ■ ■ □
□ ■ ■ ■ ■ ■ ■ □
□ ■ ■ ■ ■ ■ ■ □
□ ■ ■ ■ ■ ■ ■ □
□ ■ ■ ■ ■ ■ ■ □
□ □ □ □ □ □ □ □因为不想用三维数组，所以36×3
*/
//指针不能指向const，喵喵
uint8_t sunny_RGB[36][3] = {//晴天
    {0, 0, 0}, {31, 30, 8}, {79, 76, 20}, {79, 76, 20}, {31, 30, 8}, {0, 0, 0},//第一列
    {31, 30, 8}, {79, 76, 20}, {100, 94, 0}, {100, 94, 0}, {79, 76, 20}, {31, 30, 8},
    {79, 76, 20}, {100, 94, 0}, {100, 70, 0}, {100, 70, 0}, {100, 94, 0}, {79, 76, 20},
    {79, 76, 20}, {100, 94, 0}, {100, 70, 0}, {100, 70, 0}, {100, 94, 0}, {79, 76, 20},
    {31, 30, 8}, {79, 76, 20}, {100, 94, 0}, {100, 94, 0}, {79, 76, 20}, {31, 30, 8},
    {0, 0, 0}, {31, 30, 8}, {79, 76, 20}, {79, 76, 20}, {31, 30, 8}, {0, 0, 0},
};
uint8_t cloudy_RGB[36][3] = {//多云
    {0, 0, 0}, {0, 0, 0}, {0, 75, 100}, {0, 75, 100}, {0, 75, 100}, {0, 0, 0},
    {0, 0, 0}, {0, 75, 100}, {0, 75, 100}, {20, 65, 78}, {0, 75, 100},{0, 0, 0},
    {0, 0, 0}, {0, 75, 100}, {20, 65, 78}, {20, 65, 78}, {0, 75, 100},{0, 0, 0},
    {0, 0, 0}, {0, 75, 100}, {0, 75, 100}, {20, 65, 78}, {0, 75, 100},{0, 0, 0},
    {0, 0, 0}, {79, 76, 20}, {0, 75, 100}, {20, 65, 78}, {0, 75, 100},{0, 0, 0},
    {79, 76, 20}, {100, 70, 0}, {79, 76, 20}, {0, 75, 100}, {0, 75, 100},{0, 0, 0},
};
uint8_t overcast_RGB[36][3] = {//阴天
    {0, 0, 0}, {0, 0, 0}, {8, 30, 71}, {8, 30, 71}, {8, 30, 71}, {0, 0, 0},
    {0, 0, 0}, {8, 30, 71}, {8, 30, 71}, {8, 30, 71}, {8, 30, 71}, {0, 0, 0},
    {0, 0, 0}, {8, 30, 71}, {8, 30, 71}, {8, 30, 71}, {8, 30, 71}, {0, 0, 0},
    {0, 0, 0}, {8, 30, 71}, {8, 30, 71}, {8, 30, 71}, {8, 30, 71}, {0, 0, 0},
    {0, 0, 0}, {0, 0, 0}, {8, 30, 71}, {8, 30, 71}, {8, 30, 71}, {0, 0, 0},
    {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {8, 30, 71}, {8, 30, 71}, {0, 0, 0},
};
uint8_t shower_RGB[36][3] = {//阵雨
    {0, 0, 0}, {0, 0, 0}, {0, 75, 100}, {0, 75, 100}, {0, 75, 100}, {0, 0, 0},
    {0, 0, 0}, {0, 75, 100}, {0, 75, 100}, {20, 65, 78}, {0, 75, 100},{30, 61, 69},
    {0, 0, 0}, {0, 75, 100}, {20, 65, 78}, {20, 65, 78}, {0, 75, 100},{0, 0, 0},
    {0, 0, 0}, {0, 75, 100}, {0, 75, 100}, {20, 65, 78}, {0, 75, 100},{0, 0, 0},
    {0, 0, 0}, {79, 76, 20}, {0, 75, 100}, {20, 65, 78}, {0, 75, 100},{30, 61, 69},
    {79, 76, 20}, {100, 70, 0}, {79, 76, 20}, {0, 75, 100}, {0, 75, 100},{0, 0, 0},
};
uint8_t light_rain_RGB[36][3] = {//小雨
    {0, 0, 0}, {0, 0, 0}, {0, 75, 100}, {0, 75, 100}, {0, 75, 100}, {0, 0, 0},
    {0, 0, 0}, {0, 75, 100}, {0, 75, 100}, {20, 65, 78}, {0, 75, 100}, {0, 0, 0},
    {0, 0, 0}, {0, 75, 100}, {20, 65, 78}, {20, 65, 78}, {0, 75, 100}, {30, 61, 69},
    {0, 0, 0}, {0, 75, 100}, {0, 75, 100}, {20, 65, 78}, {0, 75, 100}, {0, 0, 0},
    {0, 0, 0}, {0, 0, 0}, {0, 75, 100}, {20, 65, 78}, {0, 75, 100}, {0, 0, 0},
    {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 75, 100}, {0, 75, 100},{0, 0, 0},
};
uint8_t moderate_rain_RGB[36][3] = {//中雨
    {0, 0, 0}, {0, 0, 0}, {0, 75, 100}, {0, 75, 100}, {0, 75, 100}, {0, 0, 0},
    {0, 0, 0}, {0, 75, 100}, {0, 75, 100}, {20, 65, 78}, {0, 75, 100}, {30, 61, 69},
    {0, 0, 0}, {0, 75, 100}, {20, 65, 78}, {20, 65, 78}, {0, 75, 100}, {0, 0, 0},
    {0, 0, 0}, {0, 75, 100}, {0, 75, 100}, {20, 65, 78}, {0, 75, 100}, {30, 61, 69},
    {0, 0, 0}, {0, 0, 0}, {0, 75, 100}, {20, 65, 78}, {0, 75, 100}, {0, 0, 0},
    {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 75, 100}, {0, 75, 100},{0, 0, 0},
};
uint8_t heavy_rain_RGB[36][3] = {//大雨
    {0, 0, 0}, {0, 0, 0}, {0, 75, 100}, {0, 75, 100}, {0, 75, 100}, {30, 61, 69},
    {0, 0, 0}, {0, 75, 100}, {0, 75, 100}, {20, 65, 78}, {0, 75, 100}, {0, 0, 0},
    {0, 0, 0}, {0, 75, 100}, {20, 65, 78}, {20, 65, 78}, {0, 75, 100}, {30, 61, 69},
    {0, 0, 0}, {0, 75, 100}, {0, 75, 100}, {20, 65, 78}, {0, 75, 100}, {0, 0, 0},
    {0, 0, 0}, {0, 0, 0}, {0, 75, 100}, {20, 65, 78}, {0, 75, 100}, {30, 61, 69},
    {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 75, 100}, {0, 75, 100},{0, 0, 0},
};

uint8_t red = 25, blue = 25, green = 25;//测试用，记得删
const uint8_t week_pixel[3] = {23,24,39};
uint8_t led_position[8][32] = {0};

uint8_t yellow_pixel[32] = {7};
uint8_t fall_time[32] = {0};
uint32_t dft_pixel_color[8][3] = {0};
void led_strip_remap()
{
    uint8_t row = 0, col = 0;//行和列
    for(int i = 0; i < 256; i++)
    {
        if((i/8) % 2 == 0)//说明是向下的列
        {
            row = i % 16;
        }
        else if((i/8) % 2 == 1)//向上的列
        {
            row = 7 - (i % 8);
        }
        col = i / 8;//列可以直接判断
        led_position[row][col] = i;
    }
}

void color_init()
{
    for(int i = 0; i < 8; i++)//初始化频谱颜色
    {
        led_strip_hsv2rgb(220+5*i, 90, 20, dft_pixel_color[i]+0, dft_pixel_color[i]+1, dft_pixel_color[i]+2);
    }
}

void num_display(led_strip_t *strip, uint8_t col, uint8_t num)//第col列显示数字num
{
    for(int i = 0; i < 3; i++)
    {
        for(int j = 0; j < 8; j++)
        {
            if(led_num[num][i]>>j & 0x01)
            {
                if(col+i >= 0  &&  col+i <= 31)
                    ESP_ERROR_CHECK(strip->set_pixel(strip, led_position[j][col+i], red, green, blue));
            }
            else
            {
                if(col+i >= 0  &&  col+i <= 31)
                    ESP_ERROR_CHECK(strip->set_pixel(strip, led_position[j][col+i], 0, 0, 0));
            }
        }
    }
}

void centi_display(led_strip_t *strip, uint8_t col)//显示摄氏度符号
{
    for(int i = 0; i < 5; i++)
    {
        for(int j = 0; j < 8; j++)
        {
            if(led_centi[i] >> j & 0x01)
            {
                if(col+i >= 0 && col+i < 32)
                    ESP_ERROR_CHECK(strip->set_pixel(strip, led_position[j][col+i], red, green, blue));
            }
            else
            {
                if(col+i > 0 && col+i < 32)
                    ESP_ERROR_CHECK(strip->set_pixel(strip, led_position[j][col+i], 0, 0, 0));
            }
        }
    }
}

void weather_refresh(led_strip_t *strip, uint8_t *weather_info, int8_t set)
{
    uint8_t (*p_weather)[3];
    uint8_t weather_code = *(weather_info+0);
    uint8_t temperature_high = *(weather_info+1);
    uint8_t temperature_low = *(weather_info+2);
    uint8_t temp[2] = {0};
    switch(weather_code)
	{
		case 0: case 1: p_weather = sunny_RGB; break;//晴
		case 4: case 5: case 7: case 6: case 8: p_weather = cloudy_RGB; break;//多云
		case 9: p_weather = overcast_RGB; break;//阴天
		case 10: case 11: case 12: p_weather = shower_RGB; break;//阵雨
		case 13: p_weather = light_rain_RGB; break;//小雨
		case 14: p_weather = moderate_rain_RGB; break;//中雨
		case 15: case 16: case 17: case 18: p_weather = heavy_rain_RGB; break;//大雨
		//case 22:LCD_freeshow(0,4,32,4,image_Light_Snow);break;//小雪
		//case 23:LCD_freeshow(0,4,32,4,image_Moderate_Snow);break;//中雪
		//case 24:LCD_freeshow(0,4,32,4,image_Heavy_Snow);break;//大雪
		//case 25:LCD_freeshow(0,4,32,4,image_SnowStorm);break;//暴雪
		//case 26:LCD_freeshow(0,4,32,4,image_Dust);break;//浮尘
		//case 27:LCD_freeshow(0,4,32,4,image_Sand);break;//扬沙
		//case 28:case 29:LCD_freeshow(0,4,32,4,image_Duststorm);break;//沙尘暴
		//case 30:LCD_freeshow(0,4,32,4,image_Foggy);break;//雾
		//case 31:LCD_freeshow(0,4,32,4,image_Haze);break;//雾霾
		//case 32:case 33:LCD_freeshow(0,4,32,4,image_Windy);break;//风
		//case 34:case 35:LCD_freeshow(0,4,32,4,image_Hurricane);break;//飓风
		//case 36:LCD_freeshow(0,4,32,4,image_Tornado);break;//龙卷风
		default: p_weather = sunny_RGB; break;//未知
	}
    //显示天气
    for(int i = 0; i < 6; i++)
    {
        for(int j = 1; j < 7; j++)
        {
            if(i+26+set >= 0 && i+26+set < 32)
                ESP_ERROR_CHECK(strip->set_pixel(strip, led_position[j][i+26+set],                       \
                *(*(p_weather+i*6+j-1)+0), *(*(p_weather+i*6+j-1)+1),*(*(p_weather+i*6+j-1)+2) ));
        }
    }
    //显示最高温度
    temp[0] = temperature_high / 10;
    temp[1] = temperature_high % 10;
    if(temp[0] != 0)
        num_display(strip, 0+set, temp[0]);
    num_display(strip, 4+set, temp[1]);
    //显示/
    num_display(strip, 8+set, 11);
    //显示最低温度
    temp[0] = temperature_low / 10;
    temp[1] = temperature_low % 10;
    if(temp[0] != 0)
        num_display(strip, 12+set, temp[0]);
    num_display(strip, 16+set, temp[1]);
    //显示℃符号
    centi_display(strip, 20+set);
}
/*刷新时间函数
sntp_task获取时间后，在led_strip_task里执行time_refresh()
*/
void time_refresh(led_strip_t *strip, char *time, int8_t set)
{    
    uint8_t time_array[6] = {0};
    uint8_t week = 0;//星期
    //得到时间
    time_array[0] = *(time+11) - 48;
    time_array[1] = *(time+12) - 48;
    time_array[2] = *(time+14) - 48;
    time_array[3] = *(time+15) - 48;
    time_array[4] = *(time+17) - 48;
    time_array[5] = *(time+18) - 48;
    num_display(strip, 2+set, time_array[0]);
    num_display(strip, 2+4+set, time_array[1]);        //小时
    num_display(strip, 2+8+set, 10);                    //冒号
    num_display(strip, 2+10+set, time_array[2]);
    num_display(strip, 2+14+set, time_array[3]);       //分钟
    num_display(strip, 2+18+set, 10);                    //冒号
    num_display(strip, 2+20+set, time_array[4]);
    num_display(strip, 2+24+set, time_array[5]);       //秒
    //得到星期
    char s_week[4] = {0};
    s_week[0] = *time;
    s_week[1] = *(time+1);
    s_week[2] = *(time+2);
    s_week[3] = '\0';
    //ESP_LOGI(TAG_ui, "%s", s_week);
    if(strcmp(s_week,"Mon") == 0)
    {
        week = 1;
    }
    else if(strcmp(s_week,"Tue") == 0)
    {
        week = 2;
    }
    else if(strcmp(s_week,"Wed") == 0)
    {
        week = 3;
    }
    else if(strcmp(s_week,"Thu") == 0)
    {
        week = 4;
    }
    else if(strcmp(s_week,"Fri") == 0)
    {
        week = 5;
    }
    else if(strcmp(s_week,"Sat") == 0)
    {
        week = 6;
    }
    else if(strcmp(s_week,"Sun") == 0)
    {
        week = 7;
    }
    else
    {
        week = 0;
    }
    for(int i = 0; i < 7; i++)
    {
        if(i+1 == week)   //当天的颜色
        {
            for(int j = 0; j < 3; j++)
            {
                if(2+i*4+j+set >= 0 && 2+i*4+j+set < 32)
                    ESP_ERROR_CHECK(strip->set_pixel(strip, led_position[7][2+i*4+j+set], 38, 12, 19));
            }
        }
        else            //其他天的颜色
        {
            for(int j = 0; j < 3; j++)
            {
                if(2+i*4+j+set >= 0 && 2+i*4+j+set < 32)
                    ESP_ERROR_CHECK(strip->set_pixel(strip, led_position[7][2+i*4+j+set], 12, 33, 39));
            }
        }
    }
    
}

void frequency_spectrum_refresh(led_strip_t *strip, uint8_t *led_dft)
{
    //ESP_ERROR_CHECK(strip->clear(strip, 100));//全部清零
    for(int i = 0; i < 32; i++)
    {
        for(int j = 0; j < 8; j++)
        {
            if(j < *(led_dft+i))
            {
                ESP_ERROR_CHECK(strip->set_pixel(strip, led_position[7-j][i], dft_pixel_color[7-j][0],      \
                dft_pixel_color[7-j][1], dft_pixel_color[7-j][2]));
            }
            else
            {
                ESP_ERROR_CHECK(strip->set_pixel(strip, led_position[7-j][i], 0, 0, 0));
            }
            //设置顶部的黄点
            if(7-*(led_dft+i) <= yellow_pixel[i]) //如果当前高度比黄点高，那么把黄点顶上去
            {
                yellow_pixel[i] = 7-*(led_dft+i);
                fall_time[i] = 0;
            }
            else
            {
                if(++fall_time[i] == FALL_TIME)
                {
                    if(yellow_pixel[i] < 7)
                        yellow_pixel[i]++;
                    fall_time[i] = 0;
                }
            }
            ESP_ERROR_CHECK(strip->set_pixel(strip, led_position[yellow_pixel[i]][i], 40, 40, 10));
        }
    }
}

void data_refresh(led_strip_t *strip, char *time, int8_t set)
{    
    uint8_t time_array[4] = {0};
    char month[3] = {0};
    month[0] = *(time+4);
    month[1] = *(time+5);
    month[2] = *(time+6);
    month[3] = '\0';
    if(strcmp(month, "Jan") == 0)
    {
        time_array[0] = 0;
        time_array[1] = 1;
    }
    else if(strcmp(month, "Feb") == 0)
    {
        time_array[0] = 0;
        time_array[1] = 2;
    }
    else if(strcmp(month, "Mar") == 0)
    {
        time_array[0] = 0;
        time_array[1] = 3;
    }
    else if(strcmp(month, "Apr") == 0)
    {
        time_array[0] = 0;
        time_array[1] = 4;
    }
    else if(strcmp(month, "May") == 0)
    {
        time_array[0] = 0;
        time_array[1] = 5;
    }
    else if(strcmp(month, "Jun") == 0)
    {
        time_array[0] = 0;
        time_array[1] = 6;
    }
    else if(strcmp(month, "Jul") == 0)
    {
        time_array[0] = 0;
        time_array[1] = 7;
    }
    else if(strcmp(month, "Aug") == 0)
    {
        time_array[0] = 0;
        time_array[1] = 8;
    }
    else if(strcmp(month, "Sep") == 0)
    {
        time_array[0] = 0;
        time_array[1] = 9;
    }
    else if(strcmp(month, "Oct") == 0)
    {
        time_array[0] = 1;
        time_array[1] = 0;
    }
    else if(strcmp(month, "Nov") == 0)
    {
        time_array[0] = 1;
        time_array[1] = 1;
    }
    else if(strcmp(month, "Dec") == 0)
    {
        time_array[0] = 1;
        time_array[1] = 2;
    }
    if(*(time+8) == ' ')    //数组里面好像不是0，是空白
        time_array[2] = 0;
    else
        time_array[2] = *(time+8) - 48;
    time_array[3] = *(time+9) - 48; //日期
    
    num_display(strip, 5+set, time_array[0]);
    num_display(strip, 9+set, time_array[1]);//月份
    num_display(strip, 13+set, 11);         ///
    num_display(strip, 17+set, time_array[2]);
    num_display(strip, 21+set, time_array[3]);//日期
}