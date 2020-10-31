#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "my_tasks.h"
#include "shared.h"
#include "usbd_hid.h"
#include "ssd1306.h"
#include "fonts.h"
#include "neopixel.h"
#include "buttons.h"
#include "keyboard.h"
#include "parser.h"
#include "animations.h"

#define LONG_PRESS_MS 500

uint8_t init_complete;
uint32_t last_keypress;
uint32_t next_pixel_shift = 30000;
uint8_t is_sleeping, is_in_settings;
uint32_t button_hold_start, button_hold_duration;

void draw_brightness_value()
{
    memset(temp_buf, 0, PATH_SIZE);
    sprintf(temp_buf, " %d%% ", brightness_values[brightness_index]);
    ssd1306_SetCursor((128 - strlen(temp_buf)*6)/2, 30);
    ssd1306_WriteString(temp_buf,Font_6x10,White);
    ssd1306_UpdateScreen();
}

void service_all(void)
{
  for (int i = 0; i < KEY_COUNT; ++i)
    service_press(&button_status[i]);
}

void set_brightness(void)
{
    draw_brightness_value();
    redraw_bg();
    osDelay(30);
    service_all();
    save_last_profile(p_cache.current_profile);
}

void change_brightness()
{   
    redraw_bg();
    osDelay(30);
    ssd1306_Fill(Black);
    ssd1306_SetCursor(34, 8);
    ssd1306_WriteString("Brightness",Font_6x10,White);
    ssd1306_SetCursor(25, 30);
    ssd1306_WriteString("-",Font_6x10,White);
    ssd1306_SetCursor(98, 30);
    ssd1306_WriteString("+",Font_6x10,White);
    ssd1306_SetCursor(1, 53);
    ssd1306_WriteString("Press any key to exit",Font_6x10,White);
    ssd1306_UpdateScreen();
    draw_brightness_value();
    service_all();
    while(1)
    {
      HAL_IWDG_Refresh(&hiwdg);
      keyboard_update();

      for (int i = 0; i < MAPPABLE_KEY_COUNT; ++i)
          if(is_pressed(&button_status[i]))
              return;

      if(is_pressed(&button_status[KEY_BUTTON1])) // -
      {
          brightness_index--;
          if(brightness_index < 0)
              brightness_index = 0;
          set_brightness();
      }
      if(is_pressed(&button_status[KEY_BUTTON2])) // +
      {
          brightness_index++;
          if(brightness_index >= BRIGHTNESS_LEVELS)
              brightness_index = BRIGHTNESS_LEVELS - 1;
          set_brightness();
      }
      osDelay(50);
    }
}

#define MAX_PQS_PAGES 2
void profile_quickswitch(void)
{
  int8_t pqs_page = p_cache.current_profile / MAPPABLE_KEY_COUNT;
  if(pqs_page >= MAX_PQS_PAGES)
    pqs_page = MAX_PQS_PAGES - 1;
  profile_quickswitch_animation();
  list_profiles(pqs_page);
  service_all();

  while(1)
  {
    HAL_IWDG_Refresh(&hiwdg);
    keyboard_update();

    for (int i = 0; i < MAPPABLE_KEY_COUNT; ++i)
      if(is_pressed(&button_status[i]))
      {
        uint8_t this_profile = pqs_page * MAPPABLE_KEY_COUNT + i + 1;
        if(p_cache.available_profile[this_profile])
        {
          restore_profile(this_profile);
          return;
        }
        service_all();
      }
      

    if(is_pressed(&button_status[KEY_BUTTON1])) // -
    {
        pqs_page--;
        if(pqs_page < 0)
            pqs_page = MAX_PQS_PAGES-1;
        list_profiles(pqs_page);
        service_all();
    }
    if(is_pressed(&button_status[KEY_BUTTON2])) // +
    {
        pqs_page++;
        if(pqs_page >= MAX_PQS_PAGES)
            pqs_page = 0;
        list_profiles(pqs_page);
        service_all();
    }
    osDelay(50);
  }
}

void handle_button_press(uint8_t button_num)
{
    button_hold_start = HAL_GetTick();
    while(1)
    {
        HAL_IWDG_Refresh(&hiwdg);
        keyboard_update();
        button_hold_duration = HAL_GetTick() - button_hold_start;
        if(button_status[button_num].button_state == BUTTON_RELEASED)
            break;
        if(button_hold_duration > LONG_PRESS_MS)
            break;
    }
    keyboard_release_all();
    media_key_release();
    memset(key_being_held, 0, MAPPABLE_KEY_COUNT);
    if(button_hold_duration < LONG_PRESS_MS) // short press
    {
        if(button_num == KEY_BUTTON1) // -
          change_profile(PREV_PROFILE);
        else if(button_num == KEY_BUTTON2) // +
          change_profile(NEXT_PROFILE);
    }
    else // long press
    {
      is_in_settings = 1;
      if(button_num == KEY_BUTTON1) // -
      {
        change_brightness();
        save_settings();
      }
      else if(button_num == KEY_BUTTON2) // +
      {
        profile_quickswitch();
      }

      is_in_settings = 0;
      print_legend(0, 0);
      service_all();
    }
}

void enter_config(void)
{
  keyboard_update();
  if(is_pressed(&button_status[0]) == 0)
    return;
  all_led_off();
  osDelay(50);
  service_all();
  is_in_settings = 1;
  ssd1306_Fill(Black);
  ssd1306_SetCursor(19, 0);
  ssd1306_WriteString("Keyboard Layout:",Font_6x10,White);
  ssd1306_SetCursor(10, 12+3);
  ssd1306_WriteString("1: WQERTY",Font_6x10,White);
  ssd1306_SetCursor(10, 24+3);
  ssd1306_WriteString("2: AZERTY(FR)",Font_6x10,White);
  ssd1306_SetCursor(10, 36+3);
  ssd1306_WriteString("3: AZERTY(BE)",Font_6x10,White);
  ssd1306_SetCursor(10, 48+3);
  ssd1306_WriteString("4: DVORAK",Font_6x10,White);
  ssd1306_UpdateScreen();
  while(1)
  {
    keyboard_update();
    HAL_IWDG_Refresh(&hiwdg);
    if(is_pressed(&button_status[0]))
    {
      curr_kb_layout = KB_LAYOUT_WQERTY;
      break;
    }
    else if(is_pressed(&button_status[1]))
    {
      curr_kb_layout = KB_LAYOUT_AZERTY_FR;
      break;
    }
    else if(is_pressed(&button_status[2]))
    {
      curr_kb_layout = KB_LAYOUT_AZERTY_BE;
      break;
    }
    else if(is_pressed(&button_status[3]))
    {
      curr_kb_layout = KB_LAYOUT_DVORAK;
      break;
    }
  }
  save_last_profile(p_cache.current_profile);
  service_all();
  is_in_settings = 0;
  print_legend(0, 0);
  save_settings();
}

void keypress_task_start(void const * argument)
{
  while(init_complete == 0)
    osDelay(16);
  enter_config();
  change_bg();
  service_all();
  keyboard_release_all();
  for(;;)
  {
    for (int i = 0; i < KEY_COUNT; ++i)
    {
      if(is_pressed(&button_status[i]))
      {
        last_keypress = HAL_GetTick();
        ssd1306_dim(0); // OLED back to full brightness

        if(is_sleeping) // wake up from sleep
        {
          change_bg();
          restore_profile(p_cache.current_profile);
          is_sleeping = 0;
          goto key_task_end;
        }

        if(i <= KEY_14)
        {
          keydown_anime_start(i);
          handle_keypress(i, &button_status[i]); // handle the button state inside here for repeats
          if(key_being_held[i] == 0)
            keydown_anime_end(i);
        }

        else if(i == KEY_BUTTON1 || i == KEY_BUTTON2)
          handle_button_press(i);
      }
      else if(is_released_but_not_serviced(&button_status[i]))
      {
        // printf("button %d released\n", i);
        // for (int i = 0; i < MAPPABLE_KEY_COUNT; ++i)
        //   printf("%d ", key_being_held[i]);
        // printf("\n");
        last_keypress = HAL_GetTick();
        if(key_being_held[i])
        {
          keypress_wrap(i, 1);
          keydown_anime_end(i);
        }
      }
      key_task_end:
      service_press(&button_status[i]);
    } 
    osDelay(16);
  }
}

void animation_task_start(void const * argument)
{
  while(init_complete == 0)
    osDelay(20);
  anime_init();
  for(;;)
  {
    led_animation_handler();
    if(dp_settings.sleep_after_ms != 0 && is_sleeping == 0 && HAL_GetTick() - last_keypress > dp_settings.sleep_after_ms)
    {
      key_led_shutdown();
      ssd1306_Fill(Black);
      ssd1306_UpdateScreen();
      is_sleeping = 1;
    }
    // dim OLED screen after 5 minutes of idle to prevent burn-in
    if(HAL_GetTick() - last_keypress > 300000)
      ssd1306_dim(1);
    // shift pixels around every 2 minutes to prevent burn-in
    if(is_sleeping == 0 && is_in_settings == 0 && HAL_GetTick() > next_pixel_shift)
    {
      if(has_valid_profiles)
        print_legend(rand()%3-1, rand()%3-1); // -1 to 1
      next_pixel_shift = HAL_GetTick() + 120000;
    }
    osDelay(20);
  }
}
