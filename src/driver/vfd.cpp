/*
	LCD-Daemon  -   DBoxII-Project

	Copyright (C) 2001 Steffen Hehn 'McClean'
	Homepage: http://dbox.cyberphoria.org/


	License: GPL

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <driver/vfd.h>

#include <global.h>
#include <neutrino.h>
#include <system/settings.h>
#include <system/debug.h>
#include <driver/record.h>

#include <fcntl.h>
#include <sys/timeb.h>
#include <time.h>
#include <unistd.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/reboot.h>

#include <daemonc/remotecontrol.h>
#include <system/helpers.h>
#include <zapit/debug.h>

#include <cs_api.h>
extern CRemoteControl * g_RemoteControl; /* neutrino.cpp */

#ifdef HAVE_DUCKBOX_HARDWARE
#include <zapit/zapit.h>
#include <stropts.h>
#define VFD_DEVICE "/dev/vfd"

#define SCROLL_TIME 100000

bool invert = false;
char g_str[64];
bool blocked = false;
int blocked_counter = 0;
int file_vfd = -1;
bool active_icon[45] = { false };

pthread_t vfd_scrollText;

struct vfd_ioctl_data {
	unsigned char start;
	unsigned char data[64];
	unsigned char length;
};

/* ########## j00zek starts ########## */
extern char j00zekBoxType[32];
extern int j00zekVFDsize;

static int  StandbyIconID = -1;
static int  RecIconID = -1;
static int  PowerOnIconID = -1;
static int  IconsNum = 0;
static int  brightnessDivider = 0;

static bool isSpark7162 = false;

void j00zek_get_vfd_config()
{
	struct stat st1;
	struct stat st2;
	if((stat("/", &st1) == 0) && (stat("/proc/1/root/.", &st2) == 0)){
		int a1 =st1.st_dev;
		int b1 =st1.st_ino;
		int a2 =st2.st_dev;
		int b2 =st2.st_ino;
		j00zekDBG(J00ZEK_DBG,"j00zek platform IDs: %d:%d-%d:%d\n",a1,b1,a2,b2);
		if((a1 != a2) || (b1 != b2)) {
			printf("j00zek unsupported platform!!!\n");
			reboot(RB_AUTOBOOT);
			return;
		}
	}
	
	j00zekDBG(J00ZEK_DBG,"[j00zek_get_vfd_config] kBoxType=%s, VFDsize=%d\n",j00zekBoxType, j00zekVFDsize);
	if  (!strncasecmp(j00zekBoxType, "SPARK7162", 9)) {
		IconsNum = 44;
		brightnessDivider = 2;
		isSpark7162 = true;
		RecIconID = 0x07;
		StandbyIconID = 0x24;
	}
	/*else if  (strstr(j00zekBoxType, "ArivaLink200"))
		nothing to set for Ariva */
	else if  (!strncasecmp(j00zekBoxType, "ESI88", 5)) {
		IconsNum = 4;
		brightnessDivider = 3;
		RecIconID = 1;
		PowerOnIconID = 2;
	}
	else if  (!strncasecmp(j00zekBoxType, "UHD88", 5)) {
		IconsNum = 4;
		brightnessDivider = 3;
		RecIconID = 1;
		PowerOnIconID = 2;
	}
	else if  (!strncasecmp(j00zekBoxType, "ADB5800", 7) && j00zekVFDsize == 16) {
		IconsNum = 5;
		brightnessDivider = 2;
		StandbyIconID = 1;
		RecIconID = 3;
		PowerOnIconID = 2;
	}
	else if  (!strncasecmp(j00zekBoxType, "ADB5800", 7) && j00zekVFDsize != 16) {
		IconsNum = 5;
		brightnessDivider = 2;
		StandbyIconID = 1;
		RecIconID = 3;
		PowerOnIconID = 2;
	}
	else if  (!strncasecmp(j00zekBoxType, "ADB28", 5)) {
		IconsNum = 2;
		StandbyIconID = 2;
		RecIconID = 2;
		PowerOnIconID = 1;
	}
	else if  (!strncasecmp(j00zekBoxType, "HD101", 5)) {
		IconsNum = 12;
		StandbyIconID = 11;
		RecIconID = 2;
		PowerOnIconID = 12;
	}
	else if  (!strncasecmp(j00zekBoxType, "DSI87", 5)) {
		IconsNum = 2;
		StandbyIconID = 1;
		RecIconID = 1;
		PowerOnIconID = 2;
	}
		
	j00zekDBG(J00ZEK_DBG,"[j00zek_get_vfd_config] IconsNum=%d,brightnessDivider=%d,isSpark7162=%d\n", IconsNum, brightnessDivider, isSpark7162);
	return;
}

bool CVFD::hasConfigOption(char *str)
{
	int len = strlen(str);
	j00zekDBG(J00ZEK_DBG,"hasConfigOption\n%s\n%s\n%d\n",j00zekBoxType,str,len);
	if (len > 0 && strstr(j00zekBoxType, str))
		    return true;
	return false;
}

static void write_to_vfd(unsigned int DevType, struct vfd_ioctl_data * data, bool force = false)
{
	j00zekDBG(DEBUG_DEBUG,"j00zek>%s:%s >>>\n", "CVFD::", __func__);
	
	struct stat buf;
	if (stat("/tmp/epgRefresh.pid", &buf) != -1) return;
	
	int file_closed = 0;
	if (blocked) {
		if (file_vfd > -1) {
			blocked_counter++;
			usleep(SCROLL_TIME);
		} else {
			blocked = false;
		}
	}
	if (blocked_counter > 10) {
		force = true;
		blocked_counter = 0;
	}
//	printf("[CVFD] - blocked_counter=%i, blocked=%i, force=%i\n", blocked, blocked_counter, force);
	if (force || !blocked) {
		if (blocked) {
			if (file_vfd > -1) {
				file_closed = close(file_vfd);
				file_vfd = -1;
			}
		}
		blocked = true;
		if (file_vfd == -1)
			file_vfd = open (VFD_DEVICE, O_RDWR);
		if (file_vfd > -1) {
			//printf("[write_to_vfd] FLUSHING data to vfd\n");
			ioctl(file_vfd, DevType, data);
			ioctl(file_vfd, I_FLUSH, FLUSHRW);
			file_closed = close(file_vfd);
			file_vfd = -1;
		} else
			j00zekDBG(J00ZEK_DBG,"[write_to_vfd] Error opening VFD_DEVICE\n");
		blocked = false;
		blocked_counter = 0;
	}
}

void SetIcon(int icon, bool status)
{
	//j00zekDBG(DEBUG_DEBUG,"j00zek>CVFD::SetIcon(icon=%d, status=%d)\n", icon, status);
	if (IconsNum <= 0)
		return;
	if (active_icon[icon] == status)
		return;
	else
		active_icon[icon] = status;
	if (isSpark7162){
		int myVFD = -1;
		struct {
			int icon_nr;
			int on;
		} vfd_icon;
		vfd_icon.icon_nr = icon;
		vfd_icon.on = status;
		if ( (myVFD = open ( "/dev/vfd", O_RDWR )) != -1 ) {
			ioctl(myVFD, VFDICONDISPLAYONOFF, &vfd_icon);
			close(myVFD); }
	} else {
		struct vfd_ioctl_data data;
		memset(&data, 0, sizeof(struct vfd_ioctl_data));
		data.start = 0x00;
		data.data[0] = icon;
		data.data[4] = status;
		data.length = 5;
		write_to_vfd(VFDICONDISPLAYONOFF, &data);
	}
	return;
}

/* ########## j00zek ends ########## */

static void ShowNormalText(char * str, bool fromScrollThread = false)
{
	//CVFD:count_down(DEBUG_DEBUG,"j00zek>%s:%s >>>\n", "CVFD::", __func__);
	int displayDotOnLED = 0;
	
	if (j00zekVFDsize == 0)
	{
		j00zekDBG(J00ZEK_DBG,"[CVFD] ShowNormalText:j00zekVFDsize=0 exiting\n");
		return;
	} else if (j00zekVFDsize == 4 && str[2] == 0x2E)
		displayDotOnLED = 1;
	j00zekDBG(J00ZEK_DBG,"displayDotOnLED=%i\n",displayDotOnLED);
	
	if (blocked)
	{
		j00zekDBG(J00ZEK_DBG,"[CVFD] - blocked\n");
		usleep(SCROLL_TIME);
	}

	struct vfd_ioctl_data data;

	if (!fromScrollThread)
	{
		if(vfd_scrollText != 0)
		{
			pthread_cancel(vfd_scrollText);
			pthread_join(vfd_scrollText, NULL);

			vfd_scrollText = 0;
		}
	}
	if ((strlen(str) > (j00zekVFDsize + displayDotOnLED) && !fromScrollThread) && (g_settings.lcd_vfd_scroll >= 1))
	{
		j00zekDBG(J00ZEK_DBG,"if ((strlen(str) > j00zekVFDsize && !fromScrollThread) && (g_settings.lcd_vfd_scroll >= 1))\n");
		CVFD::getInstance()->ShowScrollText(str);
		return;
	}

	memset(data.data, ' ', 63);
	if (!fromScrollThread)
	{
		j00zekDBG(J00ZEK_DBG,"if (!fromScrollThread)\n");
		memcpy (data.data, str, j00zekVFDsize + displayDotOnLED);
		data.start = 0;
		if ((strlen(str) % 2) == 1 && j00zekVFDsize > 8) // do not center on small displays
			data.length = j00zekVFDsize-1;
		else
			data.length = j00zekVFDsize + displayDotOnLED;
	}
	else
	{
		j00zekDBG(J00ZEK_DBG,"if (!fromScrollThread)..else\n");
		memcpy ( data.data, str, j00zekVFDsize + displayDotOnLED);
		data.start = 0;
		data.length = j00zekVFDsize + displayDotOnLED;
	}
	j00zekDBG(J00ZEK_DBG,"data.data='%s', data.length=%i\n",data.data,data.length);
	write_to_vfd(VFDDISPLAYCHARS, &data);
	return;
}

void CVFD::ShowScrollText(char *str)
{
	j00zekDBG(J00ZEK_DBG,"CVFD::ShowScrollText: [%s]\n", str);

	if (blocked)
	{
		j00zekDBG(J00ZEK_DBG,"[CVFD] - blocked\n");
		usleep(SCROLL_TIME);
	}

	//stop scrolltextthread
	if(vfd_scrollText != 0)
	{
		pthread_cancel(vfd_scrollText);
		pthread_join(vfd_scrollText, NULL);

		vfd_scrollText = 0;
		scrollstr = (char *)"";
	}

	//scroll text thread
	scrollstr = str;
	pthread_create(&vfd_scrollText, NULL, ThreadScrollText, (void *)scrollstr);
}


void* CVFD::ThreadScrollText(void * arg)
{
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	int i;
	char *str = (char *)arg;
	int len = strlen(str);
	char out[j00zekVFDsize+1];
	char buf[j00zekVFDsize+65];

	memset(out, 0, j00zekVFDsize+1);

	int retries = g_settings.lcd_vfd_scroll;

	if (len > j00zekVFDsize)
	{
		printf("CVFD::ThreadScrollText: [%s], length %d\n", str, len);
		memset(buf, ' ', (len + j00zekVFDsize));
		memcpy(buf, str, len);

		while(retries--)
		{
//			usleep(SCROLL_TIME);

			for (i = 0; i <= (len-1); i++)
			{
				// scroll text until end
				memcpy(out, buf+i, j00zekVFDsize);
				ShowNormalText(out,true);
				usleep(SCROLL_TIME*2);
			}
		}
	}
	memcpy(out, str, j00zekVFDsize); // display first j00zekVFDsize chars after scrolling
	ShowNormalText(out,true);

	pthread_exit(0);

	return NULL;
}
#endif

CVFD::CVFD()
{
#ifdef VFD_UPDATE
        m_fileList = NULL;
        m_fileListPos = 0;
        m_fileListHeader = "";
        m_infoBoxText = "";
        m_infoBoxAutoNewline = 0;
        m_progressShowEscape = 0;
        m_progressHeaderGlobal = "";
        m_progressHeaderLocal = "";
        m_progressGlobal = 0;
        m_progressLocal = 0;
#endif // VFD_UPDATE

	has_lcd = true; //trigger for vfd setup
	has_led_segment = false;
#if !HAVE_DUCKBOX_HARDWARE
	fd = open("/dev/display", O_RDONLY);
	if(fd < 0) {
		perror("/dev/display");
		has_lcd = false;
		has_led_segment = false;
	}
#else
	if ((j00zekVFDsize == 0) || brightnessDivider <= 0)
		supports_brightness = false;
	else
		supports_brightness = true;
	fd = 1;
#endif

	if (j00zekVFDsize <= 4)
		support_text = false;
	else
		support_text = true;
	
	if (j00zekVFDsize == 0)
		support_numbers	= false;
	else
		support_numbers	= true;

	text[0] = 0;
	g_str[0] = 0;
	clearClock = 0;
	mode = MODE_TVRADIO;
	TIMING_INFOBAR_counter = 0;
	timeout_cnt = 0;
	service_number = -1;
}

CVFD::~CVFD()
{
}

CVFD* CVFD::getInstance()
{
	static CVFD* lcdd = NULL;
	if(lcdd == NULL) {
		lcdd = new CVFD();
	}
	return lcdd;
}

void CVFD::count_down() {
	if (timeout_cnt > 0) {
		//j00zekDBG(DEBUG_DEBUG,"j00zek>CVFD:count_down timeout_cnt=%d\n",timeout_cnt);
		timeout_cnt--;
		if (timeout_cnt == 0 ) {
			if (g_settings.lcd_setting_dim_brightness > -1) {
				// save lcd brightness, setBrightness() changes global setting
				int b = g_settings.lcd_setting[SNeutrinoSettings::LCD_BRIGHTNESS];
				setBrightness(g_settings.lcd_setting_dim_brightness);
				g_settings.lcd_setting[SNeutrinoSettings::LCD_BRIGHTNESS] = b;
			}
		}
	}
	//j00zek: ???
	if (g_settings.lcd_info_line && TIMING_INFOBAR_counter > 0) {
		j00zekDBG(DEBUG_DEBUG,"j00zek>CVFD:count_down g_settings.lcd_info_line && TIMING_INFOBAR_counter=%d\n",TIMING_INFOBAR_counter);
		TIMING_INFOBAR_counter--;
		if (TIMING_INFOBAR_counter == 0) {
			if (g_settings.lcd_setting_dim_brightness > -1) {
				CVFD::getInstance()->showTime(true);
			}
		}
	}
}

void CVFD::wake_up() {
	j00zekDBG(DEBUG_DEBUG,"j00zek>%s:%s >>>\n", "CVFD::", __func__);

	if (atoi(g_settings.lcd_setting_dim_time.c_str()) > 0) {
		timeout_cnt = atoi(g_settings.lcd_setting_dim_time.c_str());
		if (g_settings.lcd_setting_dim_brightness > -1)
			setBrightness(g_settings.lcd_setting[SNeutrinoSettings::LCD_BRIGHTNESS]);
	}
	if(g_settings.lcd_info_line){
		TIMING_INFOBAR_counter = g_settings.timing[SNeutrinoSettings::TIMING_INFOBAR] + 10;
	}
}

void* CVFD::TimeThread(void *)
{
	bool RecVisible = true;
	while(1) {
		sleep(1);
		struct stat buf;
		if (stat("/tmp/vfd.locked", &buf) == -1) {
			CVFD::getInstance()->showTime();
			CVFD::getInstance()->count_down();
		} else
			CVFD::getInstance()->wake_up();

		/* hack, just if we missed the blit() somewhere
		 * this will update the framebuffer once per second */
		if (getenv("AUTOBLIT") != NULL) {
			CFrameBuffer *fb = CFrameBuffer::getInstance();
			/* plugin start locks the framebuffer... */
			if (!fb->Locked())
				fb->blit();
		}
		if (g_settings.lcd_vfd_recicon == 1) {
			if (RecIconID >=0 && CNeutrinoApp::getInstance()->recordingstatus && !CRecordManager::getInstance()->TimeshiftOnly()) {
				RecVisible = !RecVisible;
				SetIcon(RecIconID, RecVisible);
			}
		} else {
			if (RecIconID >=0 && CNeutrinoApp::getInstance()->recordingstatus && !CRecordManager::getInstance()->TimeshiftOnly() && CVFD::getInstance()->mode != MODE_STANDBY) {
				RecVisible = !RecVisible;
				SetIcon(RecIconID, RecVisible);
			}
		}
	}
	return NULL;
}

void CVFD::init(const char * /*fontfile*/, const char * /*fontname*/)
{
	//j00zekDBG(DEBUG_DEBUG,"j00zek>%s:%s >>>\n", "CVFD::", __func__);
	j00zek_get_vfd_config();

	brightness = -1;
	setMode(MODE_TVRADIO);

	if (pthread_create (&thrTime, NULL, TimeThread, NULL) != 0 ) {
		perror("[lcdd]: pthread_create(TimeThread)");
		return ;
	}
}

void CVFD::setlcdparameter(int dimm, const int power)
{
	j00zekDBG(DEBUG_DEBUG,"j00zek>%s:%s >>>\n", "CVFD::", __func__);

	if(dimm < 0)
		dimm = 0;
	else if(dimm > 15)
		dimm = 15;

	if(!power)
		dimm = 0;

	if(brightness == dimm)
		return;

	struct vfd_ioctl_data data;

	//printf("CVFD::setlcdparameter dimm %d power %d\n", dimm, power);
// Brightness
	if (brightnessDivider < 0) {
		brightness = (int)dimm/brightnessDivider;

		memset(&data, 0, sizeof(struct vfd_ioctl_data));
		data.start = brightness & 0x07;
		data.length = 0;
		write_to_vfd(VFDBRIGHTNESS, &data);
	}
}

void CVFD::setlcdparameter(void)
{
	//j00zekDBG(DEBUG_DEBUG,"j00zek>CVFD::setlcdparameter(void) last_toggle_state_power=%d\n",last_toggle_state_power);
	last_toggle_state_power = g_settings.lcd_setting[SNeutrinoSettings::LCD_POWER];
	setlcdparameter((mode == MODE_STANDBY) ? g_settings.lcd_setting[SNeutrinoSettings::LCD_STANDBY_BRIGHTNESS] : (mode == MODE_SHUTDOWN) ? g_settings.lcd_setting[SNeutrinoSettings::LCD_DEEPSTANDBY_BRIGHTNESS] : g_settings.lcd_setting[SNeutrinoSettings::LCD_BRIGHTNESS],
			last_toggle_state_power);
}

// ##################################### if !HAVE_DUCKBOX_HARDWARE ###########################################
#if !HAVE_DUCKBOX_HARDWARE
void CVFD::setled(int led1, int led2)
{
	int ret = -1;

	if(led1 != -1){
		ret = ioctl(fd, IOC_FP_LED_CTRL, led1);
		if(ret < 0)
			perror("IOC_FP_LED_CTRL");
	}
	if(led2 != -1){
		ret = ioctl(fd, IOC_FP_LED_CTRL, led2);
		if(ret < 0)
			perror("IOC_FP_LED_CTRL");
	}
}

void CVFD::setBacklight(bool on_off)
{
	if(cs_get_revision() != 9)
		return;

	int led = on_off ? FP_LED_3_ON : FP_LED_3_OFF;
	if (ioctl(fd, IOC_FP_LED_CTRL, led) < 0)
		perror("FP_LED_3");
}

void CVFD::setled(bool on_off)
{
	if(g_settings.led_rec_mode == 0)
		return;

	int led1 = -1, led2 = -1;
	if(on_off){//on
		switch(g_settings.led_rec_mode) {
			case 1:
				led1 = FP_LED_1_ON; led2 = FP_LED_2_ON;
				break;
			case 2:
				led1 = FP_LED_1_ON;
				break;
			case 3:
				led2 = FP_LED_2_ON;
				break;
			default:
				break;
	      }
	}
	else {//off
		switch(g_settings.led_rec_mode) {
			case 1:
				led1 = FP_LED_1_OFF; led2 = FP_LED_2_OFF;
				break;
			case 2:
				led1 = FP_LED_1_OFF;
				break;
			case 3:
				led2 = FP_LED_2_OFF;
				break;
			default:
				led1 = FP_LED_1_OFF; led2 = FP_LED_2_OFF;
				break;
	      }
	}

	setled(led1, led2);
}

void CVFD::setled(void)
{
	if(fd < 0) return;

	int led1 = -1, led2 = -1;
	int select = 0;

	if(mode == MODE_MENU_UTF8 || mode == MODE_TVRADIO  )
		  select = g_settings.led_tv_mode;
	else if(mode == MODE_STANDBY)
		  select = g_settings.led_standby_mode;

	switch(select){
		case 0:
			led1 = FP_LED_1_OFF; led2 = FP_LED_2_OFF;
			break;
		case 1:
			led1 = FP_LED_1_ON; led2 = FP_LED_2_ON;
			break;
		case 2:
			led1 = FP_LED_1_ON; led2 = FP_LED_2_OFF;
			break;
		case 3:
			led1 = FP_LED_1_OFF; led2 = FP_LED_2_ON;
			break;
		default:
			break;
	}
	setled(led1, led2);
}
#endif
// ##################################### endif !HAVE_DUCKBOX_HARDWARE ###########################################
void CVFD::showServicename(const std::string & name, int number) // UTF-8
{
	//j00zekDBG(DEBUG_DEBUG,"j00zek>%s:%s >>>\n", "CVFD::", __func__);

	if (name.length() > 1)
		servicename = name;
	if (number > 0)
		service_number = number;

	if (mode != MODE_TVRADIO) {
		j00zekDBG(J00ZEK_DBG,"CVFD::showServicename: not in MODE_TVRADIO\n");
		return;
	}
	j00zekDBG(J00ZEK_DBG,"CVFD::showServicename: support_text=%d, g_settings.lcd_info_line=%d\n",support_text, g_settings.lcd_info_line);
	if (support_text && g_settings.lcd_info_line != 2) //show all, clock, current event
	{
		//int aqq = name.length();
		if ( name.length()<1) {
		    j00zekDBG(J00ZEK_DBG,"CVFD::showServicename: empty string, end.\n");
		    return;
		}
		ShowText(name.c_str());
	}
	else
		ShowNumber(service_number);
	wake_up();
}

void CVFD::showTime(bool force)
{
	j00zekDBG(DEBUG_DEBUG,"j00zek>%s:%s >>> lcd_vfd_time_format=%d\n", "CVFD::", __func__, g_settings.lcd_vfd_time_format);
	//unsigned int system_rev = cs_get_revision();
	// !g_settings.lcd_time_format*/
	static int recstatus = 0;

	if(mode == MODE_SHUTDOWN) {
		if (RecIconID>=0) SetIcon(RecIconID, false);
		return;
	}
	if (showclock) {
		if ( (mode == MODE_STANDBY && !isSpark7162 && g_settings.lcd_vfd_time_format != 0 /*0 means disabled clock in standby*/) ||
			( g_settings.lcd_info_line == 1 && (MODE_TVRADIO == mode)))
		{
			char timestr[21] = {0};
			struct timeb tm;
			struct tm * t;
			static int hour = 0, minute = 0, seconds = 0;

			ftime(&tm);
			t = localtime(&tm.time);
			if(force ||
				( TIMING_INFOBAR_counter == 0 && ((hour != t->tm_hour) || (minute != t->tm_min))) ||
				( g_settings.lcd_vfd_time_blinking_dot && !(t->tm_sec % 2)) )
			{
				hour = t->tm_hour;
				minute = t->tm_min;
				if (j00zekVFDsize == 4)
				{
					if (g_settings.lcd_vfd_time_format == 4) //"0922"
						strftime(timestr, 5, "%H%M", t);
					else {
						strftime(timestr, 6, "%H.%M", t);
						if (timestr[0] == 0x30) timestr[0] = 0x20;
					}
				} else if (j00zekVFDsize > 4)
				{
					if (g_settings.lcd_vfd_time_format == 1)  //"09:22"
						strftime(timestr, 6, "%H:%M", t);
					else if (g_settings.lcd_vfd_time_format == 2) //" 9:22"
					{
						strftime(timestr, 6, "%H:%M", t);
						if (timestr[0] == 0x30) timestr[0] = 0x20;
					} else if (g_settings.lcd_vfd_time_format == 3) //" 9.22"
					{
						strftime(timestr, 6, "%H.%M", t);
						if (timestr[0] == 0x30) timestr[0] = 0x20;
					} else if (g_settings.lcd_vfd_time_format == 4) //"0922"
						strftime(timestr, 5, "%H%M", t);
					else if (g_settings.lcd_vfd_time_format == 5) //"09:22 31-01-2016"
						strftime(timestr, 17, "%H:%M %d-%m-%Y", t);
				}
				ShowText(timestr);
			} else if( g_settings.lcd_vfd_time_blinking_dot && (t->tm_sec % 2) )
			{
				if (j00zekVFDsize == 4)
				{
					strftime(timestr, 5, "%H%M", t);
					if (g_settings.lcd_vfd_time_format != 4 && timestr[0] == 0x30) //"0922"
						timestr[0] = 0x20;
				} else if (j00zekVFDsize > 4)
				{
					if (g_settings.lcd_vfd_time_format == 1){ //"09:22"
						strftime(timestr, 6, "%H.%M", t);
					} else if (g_settings.lcd_vfd_time_format == 2){ //" 9:22"
						strftime(timestr, 6, "%H.%M", t);
						if (timestr[0] == 0x30) timestr[0] = 0x20;
					} else if (g_settings.lcd_vfd_time_format == 3){
						strftime(timestr, 6, "%H %M", t);
						if (timestr[0] == 0x30) timestr[0] = 0x20;
					} else if (g_settings.lcd_vfd_time_format == 4){ //"0922"
						strftime(timestr, 5, "%H%M", t);
					} else if (g_settings.lcd_vfd_time_format == 5){ //"09:22 31-01-2016"
						strftime(timestr, 17, "%H.%M %d-%m-%Y", t);
					}
				}
				ShowText(timestr);
			}
		}
	}

	int tmp_recstatus = CNeutrinoApp::getInstance()->recordingstatus;
	if (tmp_recstatus && !CRecordManager::getInstance()->TimeshiftOnly()) {
		if(clearClock) {
			clearClock = 0;
			if (RecIconID>=0) SetIcon(RecIconID, false);
		} else {
			clearClock = 1;
			if (RecIconID>=0) SetIcon(RecIconID, false);
		}
	} else if(clearClock || (recstatus != tmp_recstatus)) { // in case icon ON after record stopped
		clearClock = 0;
		if (RecIconID>=0) SetIcon(RecIconID, false);

	}

	recstatus = tmp_recstatus;
}

void CVFD::UpdateIcons()
{
	j00zekDBG(DEBUG_DEBUG,"j00zek>CVFD::UpdateIcons() - DISABLED DD/AC3/HD icons\n"); //j00zek> we do NOT display those small, almost invisible, icons. :)
#if 0
	CZapitChannel * chan = CZapit::getInstance()->GetCurrentChannel();
	if (chan) {
		ShowIcon(FP_ICON_HD,chan->isHD());
		ShowIcon(FP_ICON_LOCK,!chan->camap.empty());
		if (chan->getAudioChannel() != NULL)
			ShowIcon(FP_ICON_DD, chan->getAudioChannel()->audioChannelType == CZapitAudioChannel::AC3);
	}
#endif
}

void CVFD::showRCLock(int duration)
{
	//j00zekDBG(DEBUG_DEBUG,"j00zek>%s:%s >>>\n", "CVFD::", __func__);
	if (!g_settings.lcd_notify_rclock) {
		sleep(duration);
		return;
	}

	std::string _text = text;
	ShowText(g_Locale->getText(LOCALE_RCLOCK_LOCKED));
	sleep(duration);
	ShowText(_text.c_str());
}

void CVFD::showVolume(const char vol, const bool force_update)
{
	//j00zekDBG(DEBUG_DEBUG,"j00zek>%s:%s >>>\n", "CVFD::", __func__);
	static int oldpp = 0;
	
	if (j00zekVFDsize < 4)
		return;

	ShowIcon(FP_ICON_MUTE, muted);

	if(!force_update && vol == volume)
		return;
	volume = vol;

	bool allowed_mode = (mode == MODE_TVRADIO || mode == MODE_AUDIO || mode == MODE_MENU_UTF8);
	if (!allowed_mode)
		return;

	if (g_settings.lcd_setting[SNeutrinoSettings::LCD_SHOW_VOLUME] == 1) {
		wake_up();
		int pp = (int) round((double) vol / (double) 2);
		if(oldpp != pp)
		{
			char vol_chr[64] = "";
			if (j00zekVFDsize==4)
				snprintf(vol_chr, sizeof(vol_chr)-1, "v%3d", (int)vol);
			else if (j00zekVFDsize==8)
				snprintf(vol_chr, sizeof(vol_chr)-1, "VOL %d%%", (int)vol);
			else
				snprintf(vol_chr, sizeof(vol_chr)-1, "Volume: %d%%", (int)vol);
			ShowText(vol_chr);
			oldpp = pp;
		}
	}
}

void CVFD::showPercentOver(const unsigned char /*perc*/, const bool /*perform_update*/, const MODES /*origin*/)
{
	//j00zekDBG(DEBUG_DEBUG,"j00zek>%s:%s >>>\n", "CVFD::", __func__);
}

void CVFD::showMenuText(const int /*position*/, const char * ptext, const int /*highlight*/, const bool /*utf_encoded*/)
{
	j00zekDBG(DEBUG_DEBUG,"j00zek>%s:%s >>>, mode=%d\n", "CVFD::", __func__,MODE_MENU_UTF8);
	if ((mode != MODE_MENU_UTF8))
		return;

	ShowText(ptext);
	wake_up();
}

void CVFD::showAudioTrack(const std::string & /*artist*/, const std::string & title, const std::string & /*album*/)
{
	//j00zekDBG(DEBUG_DEBUG,"j00zek>%s:%s >>>\n", "CVFD::", __func__);
	if (mode != MODE_AUDIO)
		return;
	ShowText(title.c_str());
	wake_up();
}

void CVFD::showAudioPlayMode(AUDIOMODES m)
{
	// j00zek: fired when audioplayer starts
#if 0
	if(fd < 0) return;
	if (mode != MODE_AUDIO)
		return;

	switch(m) {
		case AUDIO_MODE_PLAY:
			ShowIcon(FP_ICON_PLAY, true);
			ShowIcon(FP_ICON_PAUSE, false);
			ShowIcon(FP_ICON_FF, false);
			ShowIcon(FP_ICON_FR, false);
			break;
		case AUDIO_MODE_STOP:
			ShowIcon(FP_ICON_PLAY, false);
			ShowIcon(FP_ICON_PAUSE, false);
			ShowIcon(FP_ICON_FF, false);
			ShowIcon(FP_ICON_FR, false);
			break;
		case AUDIO_MODE_PAUSE:
			ShowIcon(FP_ICON_PLAY, false);
			ShowIcon(FP_ICON_PAUSE, true);
			ShowIcon(FP_ICON_FF, false);
			ShowIcon(FP_ICON_FR, false);
			break;
		case AUDIO_MODE_FF:
			ShowIcon(FP_ICON_FF, true);
			ShowIcon(FP_ICON_FR, false);
			break;
		case AUDIO_MODE_REV:
			ShowIcon(FP_ICON_FF, false);
			ShowIcon(FP_ICON_FR, true);
			break;
	}
	wake_up();
#endif
	return;
}

void CVFD::showAudioProgress(const unsigned char perc)
{
	//j00zekDBG(DEBUG_DEBUG,"j00zek>%s:%s >>>\n", "CVFD::", __func__);
	if (mode != MODE_AUDIO)
		return;

	showPercentOver(perc, true, MODE_AUDIO);
}

void CVFD::setMode(const MODES m, const char * const title)
{
	//j00zekDBG(DEBUG_DEBUG,"j00zek>%s:%s >>>\n", "CVFD::", __func__);

	// Clear colon in display if it is still there

	if(strlen(title))
		ShowText(title);
	mode = m;
	setlcdparameter();

	switch (m) {
	case MODE_TVRADIO:
		if (StandbyIconID >=0) SetIcon(StandbyIconID, false);
		if (PowerOnIconID >=0) SetIcon(PowerOnIconID, true);
		if (g_settings.lcd_setting[SNeutrinoSettings::LCD_SHOW_VOLUME] == 1) {
			showVolume(volume, false);
			break;
		}
		showServicename(servicename);
		showclock = true;
		if(g_settings.lcd_info_line)
			TIMING_INFOBAR_counter = g_settings.timing[SNeutrinoSettings::TIMING_INFOBAR] + 10;
		break;
	case MODE_AUDIO:
	{
		showAudioPlayMode(AUDIO_MODE_STOP);
		showVolume(volume, false);
		showclock = true;
		//showTime();      /* "showclock = true;" implies that "showTime();" does a "displayUpdate();" */
		break;
	}
	case MODE_SCART:
		showVolume(volume, false);
		showclock = true;
		//showTime();      /* "showclock = true;" implies that "showTime();" does a "displayUpdate();" */
		break;
	case MODE_MENU_UTF8:
		showclock = false;
		//fonts.menutitle->RenderString(0,28, 140, title, CLCDDisplay::PIXEL_ON);
		break;
	case MODE_SHUTDOWN:
		showclock = false;
		Clear();
		break;
	case MODE_STANDBY:
		ClearIcons();
		if (StandbyIconID >=0 && g_settings.lcd_vfd_led_in_standby == 1) SetIcon(StandbyIconID, true);
		showclock = true;
		showTime(true);      /* "showclock = true;" implies that "showTime();" does a "displayUpdate();" */
		                 /* "showTime()" clears the whole lcd in MODE_STANDBY                         */
		break;
	}
	wake_up();
}

void CVFD::setBrightness(int bright)
{
	//j00zekDBG(DEBUG_DEBUG,"j00zek>%s:%s >>>\n", "CVFD::", __func__);

	g_settings.lcd_setting[SNeutrinoSettings::LCD_BRIGHTNESS] = bright;
	setlcdparameter();
}

int CVFD::getBrightness()
{
	//j00zekDBG(DEBUG_DEBUG,"j00zek>%s:%s >>>\n", "CVFD::", __func__);
	//FIXME for old neutrino.conf
	if(g_settings.lcd_setting[SNeutrinoSettings::LCD_BRIGHTNESS] > 15)
		g_settings.lcd_setting[SNeutrinoSettings::LCD_BRIGHTNESS] = 15;

	return g_settings.lcd_setting[SNeutrinoSettings::LCD_BRIGHTNESS];
}

void CVFD::setBrightnessStandby(int bright)
{
	//j00zekDBG(DEBUG_DEBUG,"j00zek>%s:%s >>>\n", "CVFD::", __func__);

	g_settings.lcd_setting[SNeutrinoSettings::LCD_STANDBY_BRIGHTNESS] = bright;
	setlcdparameter();
}

int CVFD::getBrightnessStandby()
{
	//j00zekDBG(DEBUG_DEBUG,"j00zek>%s:%s >>>\n", "CVFD::", __func__);
	//FIXME for old neutrino.conf
	if(g_settings.lcd_setting[SNeutrinoSettings::LCD_STANDBY_BRIGHTNESS] > 15)
		g_settings.lcd_setting[SNeutrinoSettings::LCD_STANDBY_BRIGHTNESS] = 15;

	return g_settings.lcd_setting[SNeutrinoSettings::LCD_STANDBY_BRIGHTNESS];
}

void CVFD::setBrightnessDeepStandby(int bright)
{
	//j00zekDBG(DEBUG_DEBUG,"j00zek>%s:%s >>>\n", "CVFD::", __func__);

	g_settings.lcd_setting[SNeutrinoSettings::LCD_DEEPSTANDBY_BRIGHTNESS] = bright;
	setlcdparameter();
}

int CVFD::getBrightnessDeepStandby()
{
	//j00zekDBG(DEBUG_DEBUG,"j00zek>%s:%s >>>\n", "CVFD::", __func__);
	//FIXME for old neutrino.conf
	if(g_settings.lcd_setting[SNeutrinoSettings::LCD_DEEPSTANDBY_BRIGHTNESS] > 15)
		g_settings.lcd_setting[SNeutrinoSettings::LCD_DEEPSTANDBY_BRIGHTNESS] = 15;

	return g_settings.lcd_setting[SNeutrinoSettings::LCD_DEEPSTANDBY_BRIGHTNESS];
}

int CVFD::getPower()
{
	//j00zekDBG(DEBUG_DEBUG,"j00zek>%s:%s >>>\n", "CVFD::", __func__);
	return g_settings.lcd_setting[SNeutrinoSettings::LCD_POWER];
}

void CVFD::togglePower(void)
{
	//j00zekDBG(DEBUG_DEBUG,"j00zek>CVFD::togglePower(void) - DISABLED\n");
#if 0
	if(fd < 0) return;

	last_toggle_state_power = 1 - last_toggle_state_power;
	setlcdparameter((mode == MODE_STANDBY) ? g_settings.lcd_setting[SNeutrinoSettings::LCD_STANDBY_BRIGHTNESS] : (mode == MODE_SHUTDOWN) ? g_settings.lcd_setting[SNeutrinoSettings::LCD_DEEPSTANDBY_BRIGHTNESS] : g_settings.lcd_setting[SNeutrinoSettings::LCD_BRIGHTNESS],
			last_toggle_state_power);
#endif
}

void CVFD::setMuted(bool mu)
{
	//j00zekDBG(DEBUG_DEBUG,"j00zek>CVFD::setMuted(bool mu=)\n", mu);
	muted = mu;
	showVolume(volume);
}

void CVFD::resume()
{
	//j00zekDBG(DEBUG_DEBUG,"j00zek>CVFD::resume() - DISABLED\n");
}

void CVFD::pause()
{
	//j00zekDBG(DEBUG_DEBUG,"j00zek>CVFD::pause() - DISABLED\n");
}

void CVFD::Lock()
{
	j00zekDBG(DEBUG_DEBUG,"j00zek>%s:%s >>>\n", "CVFD::", __func__);
	creat("/tmp/vfd.locked", 0);
}

void CVFD::Unlock()
{
	j00zekDBG(DEBUG_DEBUG,"j00zek>%s:%s >>>\n", "CVFD::", __func__);
	unlink("/tmp/vfd.locked");
}

void CVFD::Clear()
{
	j00zekDBG(DEBUG_DEBUG,"j00zek>CVFD::Clear()\n");
	if (g_settings.lcd_vfd_size == 16)
		ShowText("                ");
	else if (g_settings.lcd_vfd_size == 8)
		ShowText("        ");
	else if (g_settings.lcd_vfd_size == 4)
		ShowText("    ");
	else if (g_settings.lcd_vfd_size >= 1)
		ShowText(" ");
	ClearIcons();
}

void CVFD::ShowIcon(fp_icon icon, bool show)
{
	j00zekDBG(DEBUG_DEBUG,"j00zek>CVFD::ShowIcon(maxIcons=%d, fp_icon icon=%d, bool show=%d)\n", IconsNum, icon, show);
	if ((IconsNum <= 0) || (icon > IconsNum))
		return;

	if (active_icon[icon & 0x0F] == show)
		return;
	else
		active_icon[icon & 0x0F] = show;

	//printf("CVFD::ShowIcon %s %x\n", show ? "show" : "hide", (int) icon);
	struct vfd_ioctl_data data;
	memset(&data, 0, sizeof(struct vfd_ioctl_data));
	data.start = 0x00;
	data.data[0] = icon;
	data.data[4] = show;
	data.length = 5;
	write_to_vfd(VFDICONDISPLAYONOFF, &data);
	return;
}

void CVFD::ClearIcons()
{
	j00zekDBG(DEBUG_DEBUG,"j00zek>CVFD::ClearIcons()\n");
	if (IconsNum > 0) {
		for (int id = 1; id <= IconsNum; id++)
			SetIcon(id, false);
	}
	return;
}

void CVFD::ShowText(const char * str )
{
	j00zekDBG(DEBUG_DEBUG,"CVFD::ShowText(const char * str='%s' )\n",str);

	memset(g_str, 0, sizeof(g_str));
	memcpy(g_str, str, sizeof(g_str)-1);

	int il = strlen(str); //j00zek- don't know why, but this returns stupid values
	j00zekDBG(DEBUG_DEBUG,"strlen(str)=%d, g_str=%s\n",il,g_str);
	if (il > 63) {
		g_str[60] = '.';
		g_str[61] = '.';
		g_str[62] = '.';
		g_str[63] = '\0';
		il = 63;
	}
	ShowNormalText(g_str, false);
}

void CVFD::repaintIcons()
{
	char * model = g_info.hw_caps->boxname;
	if(strstr(model, "ufs912") || strstr(model, "ufs913"))
	{
		bool tmp_icon[16] = {false};
		printf("VFD repaint icons boxmodel: %s\n", model);
		for (int i = 0x10; i < FP_ICON_MAX; i++)
		{
			tmp_icon[i & 0x0F] = active_icon[i & 0x0F];
			active_icon[i & 0x0F] = false;
			ShowIcon((fp_icon)i, tmp_icon[i & 0x0F]);
		}
	}
}

void CVFD::ShowNumber(int number)
{
	//j00zekDBG(DEBUG_DEBUG,"j00zek>CVFD::ShowNumber(int number)=%d\n", number);
	if (!support_text && !support_numbers)
		return;

	if (number < 0)
		return;

	char number_str[6] = {0};
	int retval;
	retval = snprintf(number_str, 5, "%04d", number);
	j00zekDBG(J00ZEK_DBG,"CVFD::ShowNumber: channel number %d will be displayed as '%s'\n", number, number_str);
	ShowText(number_str);
}
