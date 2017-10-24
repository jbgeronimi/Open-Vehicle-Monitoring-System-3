/*
;    Project:       Open Vehicle Monitor System
;    Date:          14th March 2017
;
;    Changes:
;    1.0  Initial release
;
;    (C) 2011       Michael Stegen / Stegen Electronics
;    (C) 2011-2017  Mark Webb-Johnson
;    (C) 2011        Sonny Chen @ EPRO/DX
;
; Permission is hereby granted, free of charge, to any person obtaining a copy
; of this software and associated documentation files (the "Software"), to deal
; in the Software without restriction, including without limitation the rights
; to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
; copies of the Software, and to permit persons to whom the Software is
; furnished to do so, subject to the following conditions:
;
; The above copyright notice and this permission notice shall be included in
; all copies or substantial portions of the Software.
;
; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
; IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
; FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
; AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
; LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
; OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
; THE SOFTWARE.
*/

#include "esp_log.h"
static const char *TAG = "re";

#include <string.h>
#include "retools.h"
#include "ovms_peripherals.h"
#include "ovms.h"

re *MyRE = NULL;

static void RE_task(void *pvParameters)
  {
  re *me = (re*)pvParameters;
  me->Task();
  }

void re::Task()
  {
  CAN_frame_t frame;

  while(1)
    {
    if (xQueueReceive(m_rxqueue, &frame, (portTickType)portMAX_DELAY)==pdTRUE)
      {
      xSemaphoreTake(m_mutex, portMAX_DELAY);
      std::string key = GetKey(&frame);
      auto k = m_rmap.find(key);
      re_record_t* r;
      if (k == m_rmap.end())
        {
        r = k->second;
        r = new re_record_t;
        memset(r,0,sizeof(re_record_t));
        m_rmap[key] = r;
        }
      else
        {
        r = k->second;
        }
      memcpy(&r->last,&frame,sizeof(frame));
      r->rxcount++;
      // ESP_LOGI(TAG,"rx Key=%s Count=%d",key.c_str(),r->rxcount);
      xSemaphoreGive(m_mutex);
      }
    }
  }

std::string re::GetKey(CAN_frame_t* frame)
  {
  std::string key(frame->origin->GetName());
  key.append("/");
  
  char id[9];
  if (frame->FIR.B.FF == CAN_frame_std)
    sprintf(id,"%03x",frame->MsgID);
  else
    sprintf(id,"%08x",frame->MsgID);
  key.append(id);

  auto k = m_idmap.find(frame->MsgID);
  if (k != m_idmap.end())
    {
    uint8_t bytes = m_idmap[frame->MsgID];
    for (int j=0;j<8;j++)
      {
      if (bytes & (1<<j))
        {
        char b[4];
        sprintf(b,":%02x",frame->data.u8[j]);
        key.append(b);
        }
      }
    }
  
  return key;
  }

re::re(const char* name)
  : pcp(name)
  {
  m_started = monotonictime;
  xTaskCreatePinnedToCore(RE_task, "RE Task", 4096, (void*)this, 5, &m_task, 1);
  m_mutex = xSemaphoreCreateMutex();
  m_rxqueue = xQueueCreate(20,sizeof(CAN_frame_t));
  MyCan.RegisterListener(m_rxqueue);
  }

re::~re()
  {
  MyCan.DeregisterListener(m_rxqueue);

  Clear();
  xSemaphoreTake(m_mutex, portMAX_DELAY);
  vQueueDelete(m_rxqueue);
  vTaskDelete(m_task);
  xSemaphoreGive(m_mutex);
  vSemaphoreDelete(m_mutex);
  }

void re::SetPowerMode(PowerMode powermode)
  {
  m_powermode = powermode;
  switch (powermode)
    {
    case On:
      break;
    case Sleep:
      break;
    case DeepSleep:
      break;
    case Off:
      break;
    default:
      break;
    }
  }

void re::Lock()
  {
  xSemaphoreTake(m_mutex, portMAX_DELAY);
  }

void re::Unlock()
  {
  xSemaphoreGive(m_mutex);
  }

void re::Clear()
  {
  xSemaphoreTake(m_mutex, portMAX_DELAY);
  for (re_record_map_t::iterator it=m_rmap.begin(); it!=m_rmap.end(); ++it)
    {
    delete it->second;
    }
  m_rmap.clear();
  m_started = monotonictime;
  xSemaphoreGive(m_mutex);
  }

void re_start(int verbosity, OvmsWriter* writer, OvmsCommand* cmd, int argc, const char* const* argv)
  {
  if (MyRE)
    writer->puts("Error: RE tools already running");
  else
    MyRE = new re("re");
  }

void re_stop(int verbosity, OvmsWriter* writer, OvmsCommand* cmd, int argc, const char* const* argv)
  {
  if (!MyRE)
    writer->puts("Error: RE tools not running");
  else
    {
    delete MyRE;
    MyRE = NULL;
    }
  }

void re_clear(int verbosity, OvmsWriter* writer, OvmsCommand* cmd, int argc, const char* const* argv)
  {
  if (!MyRE)
    writer->puts("Error: RE tools not running");
  else
    MyRE->Clear();
  }

void re_list(int verbosity, OvmsWriter* writer, OvmsCommand* cmd, int argc, const char* const* argv)
  {
  if (!MyRE)
    {
    writer->puts("Error: RE tools not running");
    return;
    }

  uint32_t tdiff = (monotonictime - MyRE->m_started)*1000;
  if (tdiff == 0) tdiff = 1000;

  writer->printf("%-20.20s %10s %6s %s\n","key","records","ms","last");
  MyRE->Lock();
  for (re_record_map_t::iterator it=MyRE->m_rmap.begin(); it!=MyRE->m_rmap.end(); ++it)
    {
    char vbuf[30];
    char *s = vbuf;
    for (int k=0; (k < it->second->last.FIR.B.DLC) && (k < 8); k++)
      s += sprintf(s, "%02x ", it->second->last.data.u8[k]);
    if ((argc==0)||(strstr(it->first.c_str(),argv[0])))
      {
      writer->printf("%-20s %10d %6d %s\n",
        it->first.c_str(),it->second->rxcount,(tdiff/it->second->rxcount),vbuf);
      }
    }  
  MyRE->Unlock();
  }

void re_keyclear(int verbosity, OvmsWriter* writer, OvmsCommand* cmd, int argc, const char* const* argv)
  {
  if (!MyRE)
    {
    writer->puts("Error: RE tools not running");
    return;
    }

  uint32_t id = (uint32_t)strtol(argv[0],NULL,16);
  MyRE->Lock();
  auto k = MyRE->m_idmap.find(id);
  if (k != MyRE->m_idmap.end())
    {
    MyRE->m_idmap.erase(k);
    writer->puts("Cleared ID key");
    }
  MyRE->Unlock();
  }

void re_keyset(int verbosity, OvmsWriter* writer, OvmsCommand* cmd, int argc, const char* const* argv)
  {
  if (!MyRE)
    {
    writer->puts("Error: RE tools not running");
    return;
    }

  uint32_t id = (uint32_t)strtol(argv[0],NULL,16);
  uint8_t bytes = 0;
  for (int k=1;k<argc;k++)
    {
    int b = atoi(argv[k]);
    if ((b>=1)&&(b<=8))
      {
      bytes |= (1<<(b-1));
      }
    }
  MyRE->Lock();
  MyRE->m_idmap[id] = bytes;
  writer->printf("Set ID %x to bytes 0x%02x\n",id);
  MyRE->Unlock();
  }

class REInit
  {
  public: REInit();
} REInit  __attribute__ ((init_priority (8800)));

REInit::REInit()
  {
  ESP_LOGI(TAG, "Initialising RE Tools (8800)");

  OvmsCommand* cmd_re = MyCommandApp.RegisterCommand("re","RE framework",NULL, "", 0, 0, true);
  cmd_re->RegisterCommand("start","Start RE tools",re_start, "", 0, 0, true);
  cmd_re->RegisterCommand("stop","Stop RE tools",re_stop, "", 0, 0, true);
  cmd_re->RegisterCommand("clear","Clear RE records",re_clear, "", 0, 0, true);
  cmd_re->RegisterCommand("list","List RE records",re_list, "", 0, 1, true);
  OvmsCommand* cmd_rekey = cmd_re->RegisterCommand("key","RE KEY framewrok",NULL, "", 0, 0, true);
  cmd_rekey->RegisterCommand("clear","Clear RE key",re_keyclear, "<id>", 1, 1, true);
  cmd_rekey->RegisterCommand("set","Set RE key",re_keyset, "<id> {<bytes>}", 2, 9, true);
  }
