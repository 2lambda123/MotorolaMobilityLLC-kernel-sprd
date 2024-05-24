/* SPDX-License-Identifier: GPL-2.0 */
/*
* Copyright (c) 2022 Southchip Semiconductor Technology(Shanghai) Co., Ltd.
*/
#include <linux/alarmtimer.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/power/charger-manager.h>
#include <linux/power/sprd_battery_info.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/sysfs.h>
#include <linux/pm_wakeup.h>

#define SC8989X_DRV_VERSION             "1.0.0_SC"

#define SC8989X_REG_0                   0x00
#define REG00_EN_HIZ_MASK               BIT(7)
#define REG00_EN_HIZ_SHIFT              7
#define REG00_EN_HIZ                    1
#define REG00_EXIT_HIZ                  0
#define REG00_IINDPM_MASK               GENMASK(5, 0)
#define REG00_IINDPM_SHIFT              0
#define REG00_IINDPM_BASE               100
#define REG00_IINDPM_LSB                50
#define REG00_IINDPM_MIN                100
#define REG00_IINDPM_MAX                3250

#define SC8989X_REG_1                   0x01

#define SC8989X_REG_2                   0x02

#define SC8989X_REG_3                   0x03
#define REG03_WD_RST_MASK               BIT(6)
#define REG03_OTG_MASK                  BIT(5)
#define REG03_OTG_SHIFT                 5
#define REG03_OTG_ENABLE                1
#define REG03_OTG_DISABLE               0
#define SC89890H_REG_BOOST_LIMIT_MA      750
#define REG03_CHG_MASK                  BIT(4)
#define REG03_CHG_SHIFT                 4
#define REG03_CHG_ENABLE                1
#define REG03_CHG_DISABLE               0


#define SC8989X_REG_4                   0x04
#define REG04_ICC_MASK                  GENMASK(6, 0)
#define REG04_ICC_SHIFT                 0
#define REG04_ICC_BASE                  0
#define REG04_ICC_LSB                   60
#define REG04_ICC_MIN                   0
#define REG04_ICC_MAX                   5040

#define SC8989X_REG_5                   0x05
#define REG05_ITC_MASK                  GENMASK(7, 4)
#define REG05_ITC_SHIFT                 4
#define REG05_ITC_BASE                  60
#define REG05_ITC_LSB                   60
#define REG05_ITC_MIN                   60
#define REG05_ITC_MAX                   960
#define REG05_ITERM_MASK                GENMASK(3, 0)
#define REG05_ITERM_SHIFT               0
#define REG05_ITERM_BASE                30
#define REG05_ITERM_LSB                 60
#define REG05_ITERM_MIN                 30
#define REG05_ITERM_MAX                 960

#define SC8989X_REG_6                   0x06
#define REG06_VREG_MASK                 GENMASK(7, 2)
#define REG06_VREG_SHIFT                2
#define REG06_VREG_BASE                 3840
#define REG06_VREG_LSB                  16
#define REG06_VREG_MIN                  3840
#define REG06_VREG_MAX                  4856
#define REG06_VBAT_LOW_MASK             BIT(1)
#define REG06_VBAT_LOW_SHIFT            1
#define REG06_VBAT_LOW_2P8V             0
#define REG06_VBAT_LOW_3P0V             1
#define REG06_VRECHG_MASK               BIT(0)
#define REG06_VRECHG_SHIFT              0
#define REG06_VRECHG_100MV              0
#define REG06_VRECHG_200MV              1

#define SC8989X_REG_7                   0x07
#define REG07_TWD_MASK                  GENMASK(5, 4)
#define REG07_TWD_SHIFT                 4
#define REG07_TWD_DISABLE               0
#define REG07_TWD_40S                   1
#define REG07_TWD_80S                   2
#define REG07_TWD_160S                  3
#define REG07_EN_TIMER_MASK             BIT(3)
#define REG07_EN_TIMER_SHIFT            3
#define REG07_CHG_TIMER_ENABLE          1
#define REG07_CHG_TIMER_DISABLE         0


#define SC8989X_REG_8                   0x08

#define SC8989X_REG_9                   0x09
#define REG09_BATFET_DIS_MASK           BIT(5)
#define REG09_BATFET_DIS_SHIFT          5
#define REG09_BATFET_ENABLE             0
#define REG09_BATFET_DISABLE            1

#define SC8989X_REG_A                   0x0A
#define SC8989X_REG_BOOST_MASK          GENMASK(2, 0)
#define SC8989X_REG_BOOST_SHIFT         0

#define SC8989X_REG_B                   0x0B

#define SC8989X_REG_C                   0x0C
#define REG0C_OTG_FAULT                 BIT(6)

#define SC8989X_REG_D                   0x0D
#define REG0D_FORCEVINDPM_MASK          BIT(7)
#define REG0D_FORCEVINDPM_SHIFT         7

#define REG0D_VINDPM_MASK               GENMASK(6, 0)
#define REG0D_VINDPM_BASE               2600
#define REG0D_VINDPM_LSB                100
#define REG0D_VINDPM_MIN                3900
#define REG0D_VINDPM_MAX                15300 

#define SC8989X_REG_E                   0x0E
#define SC8989X_REG_F                   0x0F
#define SC8989X_REG_10                  0x10
#define SC8989X_REG_11                  0x11
#define SC8989X_REG_12                  0x12
#define SC8989X_REG_13                  0x13

#define SC8989X_REG_14                  0x14
#define REG14_REG_RST_MASK              BIT(7)
#define REG14_REG_RST_SHIFT             7
#define REG14_REG_RESET                 1
#define REG14_VENDOR_ID_MASK            GENMASK(5, 3)
#define REG14_VENDOR_ID_SHIFT           3
#define SC8989X_VENDOR_ID               4

#define SC8989X_REG_NUM                 21

#define SC8989X_BATTERY_NAME            "sc27xx-fgu"
#define BIT_DP_DM_BC_ENB                BIT(0)
#define SC8989X_DISABLE_PIN_MASK        BIT(0)
#define SC8989X_DISABLE_PIN_MASK_2721   BIT(15)

#define SC8989X_ROLE_MASTER_DEFAULT     1
#define SC8989X_ROLE_SLAVE              2

#define SC8989X_FCHG_OVP_6V             6000
#define SC8989X_FCHG_OVP_9V             9000
#define SC8989X_FCHG_OVP_14V            14000

#define SC8989X_FAST_CHARGER_VOLTAGE_MAX    10500000
#define SC8989X_NORMAL_CHARGER_VOLTAGE_MAX  6500000

#define SC8989X_FEED_WATCHDOG_VALID_MS  50
#define SC8989X_OTG_VALID_MS            500

#define SC8989X_OTG_RETRY_TIMES         10

#define SC8989X_WAKE_UP_MS              1000
#define SC8989X_CURRENT_WORK_MS         msecs_to_jiffies(100)

#define SC8989X_DUMP_WORK_MS         msecs_to_jiffies(10000)

#define SC8989X_OTG_ALARM_TIMER_MS      15000

struct sc8989x_charger_sysfs {
	char *name;
	struct attribute_group attr_g;
	struct device_attribute attr_sc8989x_dump_reg;
	struct device_attribute attr_sc8989x_lookup_reg;
	struct device_attribute attr_sc8989x_sel_reg_id;
	struct device_attribute attr_sc8989x_reg_val;
    struct device_attribute attr_sc8989x_batfet_val;
    struct device_attribute attr_sc8989x_hizi_val;
	struct attribute *attrs[7];

	struct sc8989x_charger_info *info;
};

struct sc8989x_charge_current {
	int sdp_limit;
	int sdp_cur;
	int dcp_limit;
	int dcp_cur;
	int cdp_limit;
	int cdp_cur;
	int unknown_limit;
	int unknown_cur;
	int fchg_limit;
	int fchg_cur;
};

struct sc8989x_charger_info {
	struct i2c_client *client;
	struct device *dev;
	struct power_supply *psy_usb;
	struct sc8989x_charge_current cur;
	struct mutex lock;
	struct mutex input_limit_cur_lock;
	struct delayed_work otg_work;
	struct delayed_work wdt_work;
	struct delayed_work cur_work;
  	struct delayed_work dump_work;
	struct regmap *pmic;
	struct gpio_desc *gpiod;
	struct extcon_dev *typec_extcon;
	struct alarm otg_timer;
	struct sc8989x_charger_sysfs *sysfs;
	struct completion probe_init;
	u32 charger_detect;
	u32 charger_pd;
	u32 charger_pd_mask;
	u32 new_charge_limit_cur;
	u32 current_charge_limit_cur;
	u32 new_input_limit_cur;
	u32 current_input_limit_cur;
	u32 last_limit_cur;
	u32 actual_limit_cur;
	u32 role;
	u64 last_wdt_time;
	bool charging;
	bool need_disable_Q1;
	int termination_cur;
	bool disable_wdg;
	bool otg_enable;
	unsigned int irq_gpio;
	bool is_wireless_charge;
	bool is_charger_online;
	int reg_id;
	bool disable_power_path;
  	u32 actual_limit_voltage;
	bool probe_initialized;
	bool use_typec_extcon;
	bool shutdown_flag;
};

struct sc8989x_charger_reg_tab {
	int id;
	u32 addr;
	char *name;
};

static struct sc8989x_charger_reg_tab reg_tab[SC8989X_REG_NUM + 1] = {
	{0, SC8989X_REG_0, "EN_HIZ/EN_ILIM/IINDPM"},
    {1, SC8989X_REG_1, "DP_DRIVE/DM_DRIVE/VINDPM_OS"},
    {2, SC8989X_REG_2, "CONV_START/CONV_RATE/BOOST_FRE/ICO_EN/HVDCP_EN/FORCE_DPD/AUTO_DPDM_EN"},
    {3, SC8989X_REG_3, "FORCE_DSEL/WD_RST/OTG_CFG/CHG_CFG/VSYS_MIN/VBATMIN_SEL"},
    {4, SC8989X_REG_4, "EN_PUMPX/ICC"},
    {5, SC8989X_REG_5, "ITC/ITERM"},
    {6, SC8989X_REG_6, "CV/VBAT_LOW/VRECHG"},
    {7, SC8989X_REG_7, "EN_TERM/STAT_DIS/TWD/EN_TIMER/TCHG/JEITA_ISET"},
    {8, SC8989X_REG_8, "BAT_COMP/VCLAMP/TJREG"},
    {9, SC8989X_REG_9, "FORCE_ICO/TMR2X_EN/BATFET_DIS/JEITA_VSET_WARM/BATGET_DLY/"
                "BATFET_RST_EN/PUMPX_UP/PUMPX_DN"},
    {10, SC8989X_REG_A, "V_OTG/PFM_OTG_DIS/IBOOST_LIM"},
    {11, SC8989X_REG_B, "VBUS_STAT/CHRG_STAT/PG_STAT/VSYS_STAT"},
    {12, SC8989X_REG_C, "JWD_FAULT/OTG_FAULT/CHRG_FAULT/BAT_FAULT/NTC_FAULT"},
    {13, SC8989X_REG_D, "FORCE_VINDPM/VINDPM"},
    {14, SC8989X_REG_E, "THERMAL_STAT/VBAT"},
    {15, SC8989X_REG_F, "VSYS"},
    {16, SC8989X_REG_10, "NTC"},
    {17, SC8989X_REG_11, "VBUS_GD/VBUS"},
    {18, SC8989X_REG_12, "ICC"},
    {19, SC8989X_REG_13, "VINDPM_STAT/IINDPM_STAT/IDPM_ICO"},
    {20, SC8989X_REG_14, "REG_RST/ICO_STAT/PN/NTC_PROFILE/DEV_VERSION"},
    {21, 0, "null"},
};

static bool enable_dump_stack;
module_param(enable_dump_stack, bool, 0644);


static void power_path_control(struct sc8989x_charger_info *info)
{
	/*struct device_node *cmdline_node;
	const char *cmd_line;
	int ret;
	char *match;
	char result[5];

	cmdline_node = of_find_node_by_path("/chosen");
	ret = of_property_read_string(cmdline_node, "bootargs", &cmd_line);
	if (ret) {
		info->disable_power_path = false;
		return;
	}

	if (strncmp(cmd_line, "charger", strlen("charger")) == 0)
		info->disable_power_path = true;

	match = strstr(cmd_line, "sprdboot.mode=");
	if (match) {
		memcpy(result, (match + strlen("sprdboot.mode=")),
			sizeof(result) - 1);
		if ((!strcmp(result, "cali")) || (!strcmp(result, "auto")))
			info->disable_power_path = true;
	}*/
    pr_err("%s:line%d: \n", __func__, __LINE__);
}

static int sc8989x_charger_set_limit_current(struct sc8989x_charger_info *info,
					     u32 limit_cur, bool enable);
static u32 sc8989x_charger_get_limit_current(struct sc8989x_charger_info *info,
					     u32 *limit_cur);

static bool sc8989x_charger_is_bat_present(struct sc8989x_charger_info *info)
{
	struct power_supply *psy;
	union power_supply_propval val;
	bool present = false;
	int ret;
	dev_err(info->dev, "find sc8989x_charger_is_bat_present\n");
	psy = power_supply_get_by_name(SC8989X_BATTERY_NAME);
	if (!psy) {
		dev_err(info->dev, "Failed to get psy of sc27xx_fgu\n");
		return present;
	}

	val.intval = 0;
	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_PRESENT,
					&val);
	if (ret == 0 && val.intval)
		present = true;
	power_supply_put(psy);

	if (ret)
		dev_err(info->dev,
			"Failed to get property of present:%d\n", ret);

	return present;
}

static int sc8989x_charger_is_fgu_present(struct sc8989x_charger_info *info)
{
	struct power_supply *psy;
	dev_err(info->dev, "find sc8989x_charger_is_fgu_present\n");
	psy = power_supply_get_by_name(SC8989X_BATTERY_NAME);
	if (!psy) {
		dev_err(info->dev, "Failed to find psy of sc27xx_fgu\n");
		return -ENODEV;
	}
	power_supply_put(psy);

	return 0;
}

static int sc8989x_read(struct sc8989x_charger_info *info, u8 reg, u8 *data)
{
	int ret;

	ret = i2c_smbus_read_byte_data(info->client, reg);
	if (ret < 0)
		return ret;

	*data = ret;
	return 0;
}

static int sc8989x_write(struct sc8989x_charger_info *info, u8 reg, u8 data)
{
	return i2c_smbus_write_byte_data(info->client, reg, data);
}

static int sc8989x_update_bits(struct sc8989x_charger_info *info, u8 reg,
			       u8 mask, u8 data)
{
	u8 v;
	int ret;

	ret = sc8989x_read(info, reg, &v);
	if (ret < 0)
		return ret;

	v &= ~mask;
	v |= (data & mask);

	return sc8989x_write(info, reg, v);
}

static int sc8989x_charger_get_vendor_id_part_value(struct sc8989x_charger_info *info)
{
    u8 reg_val;
    u8 reg_part_val;
    int ret;

    ret = sc8989x_read(info, SC8989X_REG_14, &reg_val);
    if (ret < 0) {
        dev_err(info->dev, "Failed to get vendor id, ret = %d\n", ret);
        return ret;
    }
    reg_part_val = reg_val;

    reg_val &= REG14_VENDOR_ID_MASK;
    reg_val >>= REG14_VENDOR_ID_SHIFT;
    if (reg_val != SC8989X_VENDOR_ID) {
        dev_err(info->dev, "The vendor id is 0x%x\n", reg_val);
        return -EINVAL;
    }

    return 0;
}

static int
sc8989x_charger_set_vindpm(struct sc8989x_charger_info *info, u32 vol)
{
	u8 reg_val;

	if (vol < REG0D_VINDPM_MIN)
        vol = REG0D_VINDPM_MIN;
    else if (vol > REG0D_VINDPM_MAX)
        vol = REG0D_VINDPM_MAX;
    
    reg_val = (vol - REG0D_VINDPM_BASE) / REG0D_VINDPM_LSB;

    return sc8989x_update_bits(info, SC8989X_REG_D,
                REG0D_VINDPM_MASK, reg_val);
}

static int
sc8989x_charger_set_ovp(struct sc8989x_charger_info *info, u32 vol)
{
	//default 14.2V
    return 0;
}

static int
sc8989x_charger_set_termina_vol(struct sc8989x_charger_info *info, u32 vol)
{
	int ret;
    u8 reg_val;
    
    if (vol < REG06_VREG_MIN)
        vol = REG06_VREG_MIN;
    else if (vol > REG06_VREG_MAX)
        vol = REG06_VREG_MAX;

    reg_val = (vol - REG06_VREG_BASE) / REG06_VREG_LSB;

    ret = sc8989x_update_bits(info, SC8989X_REG_6, REG06_VREG_MASK,
                reg_val << REG06_VREG_SHIFT);
    if (ret != 0) {
        dev_err(info->dev, "sc8989x set failed\n");
    } else {
        info->actual_limit_voltage = 
            (reg_val * REG06_VREG_LSB) + REG06_VREG_BASE;
        dev_err(info->dev, "sc8989x set success, the value is %d\n", 
            info->actual_limit_voltage);
    }

    return ret;
}

static int
sc8989x_charger_set_termina_cur(struct sc8989x_charger_info *info, u32 cur)
{
	u8 reg_val;

    if (cur < REG05_ITERM_MIN)
        cur = REG05_ITERM_MIN;
    else if (cur > REG05_ITERM_MAX)
        cur = REG05_ITERM_MAX;
    
    reg_val = (cur - REG05_ITERM_BASE) / REG05_ITERM_LSB;

    return sc8989x_update_bits(info, SC8989X_REG_5,
                REG05_ITERM_MASK, reg_val << REG05_ITERM_SHIFT);
}

static int sc8989x_charger_set_recharge(struct sc8989x_charger_info *info, 
            u32 mv)
{
    u8 reg_val = REG06_VRECHG_200MV;

    if (mv < 200) {
        reg_val = REG06_VRECHG_100MV;
    }

    return sc8989x_update_bits(info, SC8989X_REG_6,
                REG06_VRECHG_MASK,
                reg_val << REG06_VRECHG_SHIFT);
}

static int sc8989x_charger_en_chg_timer(struct sc8989x_charger_info *info, 
            bool val)
{
    int ret = 0;
    u8 reg_val = val ? REG07_CHG_TIMER_ENABLE : REG07_CHG_TIMER_DISABLE;

    if (!info) {
        pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
        return -EINVAL;
    }

    pr_info("SC8989X EN_TIMER is %s\n", val ? "enable" : "disable");

    ret = sc8989x_update_bits(info, SC8989X_REG_7,
            REG07_EN_TIMER_MASK,
            reg_val << REG07_EN_TIMER_SHIFT);
    if (ret) {
        pr_err("%s: set SC8989X chg_timer failed\n", __func__);
    }

    return ret;
}

static int sc8989x_charger_set_wd_timer(struct sc8989x_charger_info *info,
            int time)
{
    u8 reg_val;

    if (time == 0) {
        reg_val = REG07_TWD_DISABLE;
    } else if (time <= 40) {
        reg_val = REG07_TWD_40S;
    } else if (time <= 80) {
        reg_val = REG07_TWD_80S;
    } else {
        reg_val = REG07_TWD_160S;
    }

    return sc8989x_update_bits(info, SC8989X_REG_7, REG07_TWD_MASK, 
                reg_val << REG07_TWD_SHIFT);
}

static int sc8989x_otg_boost_lim(struct sc8989x_charger_info *info,
            u32 cur)
{
    u8 reg_val = 0;

    reg_val = cur;

    return sc8989x_update_bits(info, SC8989X_REG_3, REG03_CHG_MASK,
                reg_val << REG03_CHG_SHIFT);
}


static int sc8989x_charger_set_chg_en(struct sc8989x_charger_info *info, 
            bool enable)
{
    u8 reg_val = enable ? REG03_CHG_ENABLE : REG03_CHG_DISABLE;

    return sc8989x_update_bits(info, SC8989X_REG_3, REG03_CHG_MASK, 
                reg_val << REG03_CHG_SHIFT);
}

static int sc8989x_charger_set_otg_en(struct sc8989x_charger_info *info, 
            bool enable)
{
    u8 reg_val = enable ? REG03_OTG_ENABLE : REG03_OTG_DISABLE;

    return sc8989x_update_bits(info, SC8989X_REG_3, REG03_OTG_MASK, 
                reg_val << REG03_OTG_SHIFT);
}

static int sc8989x_charger_hw_init(struct sc8989x_charger_info *info)
{
	//struct power_supply_battery_info bat_info = { };
    struct sprd_battery_info bat_info = {};
    int voltage_max_microvolt, termination_cur;
    int ret ;
    // u8 batfetresetvalue;
    //int bat_id = 0;
	dev_err(info->dev, "find sc8989x_charger_hw_init\n");
    //bat_id = battery_get_bat_id();
    //ret = power_supply_get_battery_info(info->psy_usb, &bat_info);
    ret = sprd_battery_get_battery_info(info->psy_usb, &bat_info);
    // ret = power_supply_get_battery_info(info->psy_usb, &bat_info, 0);
    if (ret) {
        dev_warn(info->dev, "no battery information is supplied\n");
        pr_err("%s:ret=%d line%d: \n", __func__, ret, __LINE__);
        /*
        * If no battery information is supplied, we should set
        * default charge termination current to 100 mA, and default
        * charge termination voltage to 4.2V.
        */
        info->cur.sdp_limit = 500000;
        info->cur.sdp_cur = 500000;
        info->cur.dcp_limit = 5000000;
        info->cur.dcp_cur = 500000;
        info->cur.cdp_limit = 5000000;
        info->cur.cdp_cur = 1500000;
        info->cur.unknown_limit = 5000000;
        info->cur.unknown_cur = 500000;
        /*
		 * If no battery information is supplied, we should set
		 * default charge termination current to 120 mA, and default
		 * charge termination voltage to 4.44V.
		 */
		voltage_max_microvolt = 4440;
		termination_cur = 120;
		info->termination_cur = termination_cur;
    } else {
        info->cur.sdp_limit = bat_info.cur.sdp_limit;
        info->cur.sdp_cur = bat_info.cur.sdp_cur;
        info->cur.dcp_limit = bat_info.cur.dcp_limit;
        info->cur.dcp_cur = bat_info.cur.dcp_cur;
        info->cur.cdp_limit = bat_info.cur.cdp_limit;
        info->cur.cdp_cur = bat_info.cur.cdp_cur;
        info->cur.unknown_limit = bat_info.cur.unknown_limit;
        info->cur.unknown_cur = bat_info.cur.unknown_cur;
        info->cur.fchg_limit = bat_info.cur.fchg_limit;
        info->cur.fchg_cur = bat_info.cur.fchg_cur;

        voltage_max_microvolt =
            bat_info.constant_charge_voltage_max_uv / 1000;
        termination_cur = bat_info.charge_term_current_ua / 1000;
        info->termination_cur = termination_cur;
        //power_supply_put_battery_info(info->psy_usb, &bat_info);
        sprd_battery_put_battery_info(info->psy_usb, &bat_info);

        ret = sc8989x_update_bits(info, SC8989X_REG_14,
                    REG14_REG_RST_MASK,
                    REG14_REG_RESET << REG14_REG_RST_SHIFT);
        if (ret) {
            dev_err(info->dev, "reset sc8989x failed\n");
            return ret;
        }

        pr_err("%s:ret=%d line%d: \n", __func__, ret, __LINE__);
        if (info->role == SC8989X_ROLE_MASTER_DEFAULT) {
            ret = sc8989x_charger_set_ovp(info, SC8989X_FCHG_OVP_6V);
            if (ret) {
                dev_err(info->dev, "set sc8989x ovp failed\n");
                return ret;
            }
        } else if (info->role == SC8989X_ROLE_SLAVE) {
            ret = sc8989x_charger_set_ovp(info, SC8989X_FCHG_OVP_9V);
            if (ret) {
                dev_err(info->dev, "set sc8989x slave ovp failed\n");
                return ret;
            }
        }

        ret = sc8989x_charger_set_vindpm(info, voltage_max_microvolt);
        if (ret) {
            dev_err(info->dev, "set sc8989x vindpm vol failed\n");
            return ret;
        }

        ret = sc8989x_charger_set_termina_vol(info, voltage_max_microvolt);
        if (ret) {
            dev_err(info->dev, "set sc8989x terminal vol failed\n");
            return ret;
        }

        ret = sc8989x_charger_set_termina_cur(info, termination_cur);
        if (ret) {
            dev_err(info->dev, "set sc8989x terminal cur failed\n");
            return ret;
        }

        ret = sc8989x_charger_set_limit_current(info, info->cur.unknown_cur, false);
        if (ret)
            dev_err(info->dev, "set sc8989x limit current failed\n");

        ret = sc8989x_charger_set_recharge(info, 200);
        if (ret)
            dev_err(info->dev, "failed to set rechg volt\n");

        ret = sc8989x_charger_en_chg_timer(info, false);
        if (ret)
            dev_err(info->dev, "failed to disable chg_timer\n");

        ret = sc8989x_otg_boost_lim(info, SC89890H_REG_BOOST_LIMIT_MA);
    if (ret)
        dev_err(info->dev, "failed to set boost lim\n");
    }

    info->current_charge_limit_cur = REG04_ICC_LSB * 1000;
    info->current_input_limit_cur = REG00_IINDPM_LSB * 1000;

    dev_err(info->dev, "init sc8989x southchip\n");
    return ret;
}

static int sc8989x_enter_hiz_mode(struct sc8989x_charger_info *info)
{
    int ret;

    ret = sc8989x_update_bits(info, SC8989X_REG_0,
                REG00_EN_HIZ_MASK, REG00_EN_HIZ << REG00_EN_HIZ_SHIFT);
    if (ret)
        dev_err(info->dev, "enter HIZ mode failed\n");

    return ret;
}

static int sc8989x_exit_hiz_mode(struct sc8989x_charger_info *info)
{
    int ret;

    ret = sc8989x_update_bits(info, SC8989X_REG_0,
                REG00_EN_HIZ_MASK, REG00_EXIT_HIZ << REG00_EN_HIZ_SHIFT);
    if (ret)
        dev_err(info->dev, "exit HIZ mode failed\n");

    return ret;
}

static int sc8989x_get_hiz_status(struct sc8989x_charger_info *info)
{
    int ret;
    u8 reg_val;

    ret = sc8989x_read(info, SC8989X_REG_0, &reg_val);
    if (ret < 0)
        return ret;

    reg_val = (reg_val & REG00_EN_HIZ_MASK) >> REG00_EN_HIZ_SHIFT;
    
    return reg_val;
}

static int
sc8989x_charger_get_charge_voltage(struct sc8989x_charger_info *info,
				   u32 *charge_vol)
{
	struct power_supply *psy;
    union power_supply_propval val;
    int ret;

    psy = power_supply_get_by_name(SC8989X_BATTERY_NAME);
    if (!psy) {
        dev_err(info->dev, "failed to get SC8989X_BATTERY_NAME\n");
        return -ENODEV;
    }

    ret = power_supply_get_property(psy,
                    POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
                    &val);
    power_supply_put(psy);
    if (ret) {
        dev_err(info->dev, "failed to get CONSTANT_CHARGE_VOLTAGE\n");
        return ret;
    }

    *charge_vol = val.intval;

    return 0;
}

/*static int sc8989x_charger_enable_wdg(struct sc8989x_charger_info *info,
				      bool en)
{
	int ret;

	if (en)
		ret = sc8989x_update_bits(info, SC8989X_REG_5,
					  SC8989X_REG_WATCHDOG_TIMER_MASK,
					  0x01 << SC8989X_REG_WATCHDOG_TIMER_SHIFT);
	else
		ret = sc8989x_update_bits(info, SC8989X_REG_5,
					  SC8989X_REG_WATCHDOG_TIMER_MASK, 0);
	if (ret)
		dev_err(info->dev, "%s:Failed to update %d\n", __func__, en);

	return ret;
}*/

static int sc8989x_charger_start_charge(struct sc8989x_charger_info *info)
{
	int ret = 0;
    u8 value;
	dev_err(info->dev, "find sc8989x_charger_start_charge\n");
  	
    ret = sc8989x_read(info, SC8989X_REG_3, &value);
    if (ret) {
        dev_err(info->dev, "get sc8989x charger otg valid status failed\n");
        return ret;
    }

    if (value & REG03_OTG_MASK) {
	dev_err(info->dev, "otg online return\n");
        return ret;
    }

    ret = sc8989x_exit_hiz_mode(info);
    if (ret) {
        return ret;
    }

    ret = sc8989x_charger_set_wd_timer(info, 0);
    if (ret) {
        dev_err(info->dev, "Failed to disable sc8989x watchdog\n");
        return ret;
    }

    if (info->role == SC8989X_ROLE_MASTER_DEFAULT) {
        ret = regmap_update_bits(info->pmic, info->charger_pd,
                    info->charger_pd_mask, 0);
        if (ret) {
            dev_err(info->dev, "enable sc8989x charge failed\n");
            return ret;
        }

        ret = sc8989x_charger_set_chg_en(info , true);
        if (ret) {
            dev_err(info->dev, "enable sc8989x charge en failed\n");
            return ret;
        }
    } else if (info->role == SC8989X_ROLE_SLAVE) {
        gpiod_set_value_cansleep(info->gpiod, 0);
    }

    /*ret = sc8989x_charger_set_limit_current(info,
                        info->last_limit_cur, false);
    if (ret) {
        dev_err(info->dev, "failed to set limit current\n");
        return ret;
    }

    ret = sc8989x_charger_set_termina_cur(info, info->termination_cur);
    if (ret)
        dev_err(info->dev, "set sc8989x terminal cur failed\n");*/
    return ret;
}

static void sc8989x_charger_stop_charge(struct sc8989x_charger_info *info, bool present)
{
	int ret;
     present = sc8989x_charger_is_bat_present(info);
	dev_err(info->dev, "find sc8989x_charger_stop_charge\n");
  	cancel_delayed_work_sync(&info->dump_work);
    if (info->role == SC8989X_ROLE_MASTER_DEFAULT) {
        if (!present || info->need_disable_Q1) {
            ret = sc8989x_enter_hiz_mode(info);
            if (ret)
                dev_err(info->dev, "enable HIZ mode failed\n");

            info->need_disable_Q1 = false;
        }

        ret = regmap_update_bits(info->pmic, info->charger_pd,
                    info->charger_pd_mask,
                    info->charger_pd_mask);
        if (ret)
            dev_err(info->dev, "disable sc8989x charge failed\n");

        if (info->is_wireless_charge) {
            ret = sc8989x_charger_set_chg_en(info, false);
            if (ret)
                dev_err(info->dev, "disable sc8989x charge en failed\n");
        }
    } else if (info->role == SC8989X_ROLE_SLAVE) {
        ret = sc8989x_enter_hiz_mode(info);
        if (ret)
            dev_err(info->dev, "enable HIZ mode failed\n");

        gpiod_set_value_cansleep(info->gpiod, 1);
    }

    if (info->disable_power_path) {
        ret = sc8989x_enter_hiz_mode(info);
        if (ret)
            dev_err(info->dev, "Failed to disable power path\n");
    }

    ret = sc8989x_charger_set_wd_timer(info, 0);
    if (ret)
        dev_err(info->dev, "Failed to disable sc8989x watchdog\n");
}

static int sc8989x_charger_set_current(struct sc8989x_charger_info *info, u32 cur)
{
	u8 reg_val;
    
    cur = cur / 1000;
    dev_err(info->dev, "sc8989x set_current %d\n", cur);
    if (cur < REG04_ICC_MIN) {
        cur = REG04_ICC_MIN;
    } else if (cur > REG04_ICC_MAX) {
        cur = REG04_ICC_MAX;
    }

    reg_val = (cur - REG04_ICC_BASE) / REG04_ICC_LSB;

    return sc8989x_update_bits(info, SC8989X_REG_4, REG04_ICC_MASK, 
                reg_val << REG04_ICC_SHIFT);
}

static int sc8989x_charger_get_current(struct sc8989x_charger_info *info, u32 *cur)
{
	u8 reg_val;
    int ret;

    ret = sc8989x_read(info, SC8989X_REG_4, &reg_val);
    if (ret < 0)
        return ret;

    reg_val = (reg_val & REG04_ICC_MASK) >> REG04_ICC_SHIFT;
    
    *cur = (reg_val * REG04_ICC_LSB + REG04_ICC_BASE) * 1000;

    return 0;
}

static int sc8989x_charger_set_limit_current(struct sc8989x_charger_info *info,
					     u32 limit_cur, bool enable)
{

    u8 reg_val;
	int ret = 0;

  	limit_cur = limit_cur / 1000;
	//mutex_lock(&info->input_limit_cur_lock);
	if (enable) {
		ret = sc8989x_charger_get_limit_current(info, &limit_cur);
		if (ret) {
			dev_err(info->dev, "get limit cur failed\n");
			goto out;
		}

		if (limit_cur == info->actual_limit_cur)
			goto out;
		limit_cur = info->actual_limit_cur;
	}

	dev_err(info->dev, "%s:line%d: set limit cur = %d\n", __func__, __LINE__, limit_cur);

	if (limit_cur < REG00_IINDPM_MIN) {
        limit_cur = REG00_IINDPM_MIN;
    } else if (limit_cur > REG00_IINDPM_MAX) {
        limit_cur = REG00_IINDPM_MAX;
    }

	info->last_limit_cur = limit_cur;
	reg_val = (limit_cur - REG00_IINDPM_BASE) / REG00_IINDPM_LSB;
	ret = sc8989x_update_bits(info, SC8989X_REG_0, REG00_IINDPM_MASK,
                reg_val << REG00_IINDPM_SHIFT);
    if (ret)
        dev_err(info->dev, "set sc8989x limit cur failed\n");

    info->actual_limit_cur = 
            (reg_val * REG00_IINDPM_LSB + REG00_IINDPM_BASE) * 1000;

    return ret;

out:
	//mutex_unlock(&info->input_limit_cur_lock);

	return ret;
}

static u32 sc8989x_charger_get_limit_current(struct sc8989x_charger_info *info, u32 *limit_cur)
{
	u8 reg_val;
    int ret;

    ret = sc8989x_read(info, SC8989X_REG_0, &reg_val);
    if (ret < 0)
        return ret;

    reg_val = (reg_val & REG00_IINDPM_MASK) >> REG00_IINDPM_SHIFT;
    *limit_cur = (reg_val * REG00_IINDPM_LSB + REG00_IINDPM_BASE) * 1000;
	dev_err(info->dev, " sc8989x_charger_get_limit_current =  %d\n",*limit_cur);

    return 0;
}

static int sc8989x_charger_get_health(struct sc8989x_charger_info *info, u32 *health)
{
	*health = POWER_SUPPLY_HEALTH_GOOD;

	return 0;
}

static void sc8989x_dump_register(struct sc8989x_charger_info *info)
{
	int i, ret, len, idx = 0;
	u8 reg_val;
	char buf[500];

	memset(buf, '\0', sizeof(buf));
	for (i = 0; i < SC8989X_REG_NUM; i++) {
		ret = sc8989x_read(info,  reg_tab[i].addr, &reg_val);
		if (ret == 0) {
			len = snprintf(buf + idx, sizeof(buf) - idx,
				       "[REG_0x%.2x]=0x%.2x  ",
				       reg_tab[i].addr, reg_val);
			idx += len;
		}
	}

	dev_info(info->dev, "%s: %s", __func__, buf);
}

static void sc8989x_dump_reg_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
   struct sc8989x_charger_info *info =
        container_of(dwork, struct sc8989x_charger_info, dump_work);
   sc8989x_dump_register(info);
    schedule_delayed_work(&info->dump_work, SC8989X_DUMP_WORK_MS);
}

#if 0
static int sc8989x_charger_feed_watchdog(struct sc8989x_charger_info *info)
{
	int ret = 0;
	u64 duration, curr = ktime_to_ms(ktime_get());

	ret = sc8989x_update_bits(info, SC8989X_REG_1,
				  SC8989X_REG_WD_RST_MASK,
				  SC8989X_REG_WD_RST_MASK);
	if (ret) {
		dev_err(info->dev, "reset sc8989x failed\n");
		return ret;
	}

	duration = curr - info->last_wdt_time;
	if (duration >= SC8989X_WATCH_DOG_TIME_OUT_MS) {
		dev_err(info->dev, "charger wdg maybe time out:%lld ms\n", duration);
		sc8989x_dump_register(info);
	}

	info->last_wdt_time = curr;

	if (info->otg_enable)
		return ret;

	ret = sc8989x_charger_set_limit_current(info, 0, true);
	if (ret)
		dev_err(info->dev, "set limit cur failed\n");

	return ret;
}

static irqreturn_t sc8989x_int_handler(int irq, void *dev_id)
{
	struct sc8989x_charger_info *info = dev_id;

	if (!info) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return IRQ_HANDLED;
	}

	dev_info(info->dev, "interrupt occurs\n");
	sc8989x_dump_register(info);

	return IRQ_HANDLED;
}
#endif

static int sc8989x_charger_set_fchg_current(struct sc8989x_charger_info *info,
                        u32 val)
{
    int ret, limit_cur, cur;

    if (val == CM_PPS_CHARGE_ENABLE_CMD) {
        limit_cur = info->cur.fchg_limit;
        cur = info->cur.fchg_cur;
    } else if (val == CM_PPS_CHARGE_DISABLE_CMD) {
        limit_cur = info->cur.dcp_limit;
        cur = info->cur.dcp_cur;
    } else {
        return 0;
    }

    ret = sc8989x_charger_set_limit_current(info, limit_cur, false);
    if (ret) {
        dev_err(info->dev, "failed to set fchg limit current\n");
        return ret;
    }

    ret = sc8989x_charger_set_current(info, cur);
    if (ret) {
        dev_err(info->dev, "failed to set fchg current\n");
        return ret;
    }

    return 0;
}

static int sc8989x_charger_get_status(struct sc8989x_charger_info *info)
{
  	dev_err(info->dev, "find sc8989x_charger_get_status\n");

	if (info->charging)
		return POWER_SUPPLY_STATUS_CHARGING;
	else
		return POWER_SUPPLY_STATUS_NOT_CHARGING;
}

#if 0
static bool sc8989x_charger_get_power_path_status(struct sc8989x_charger_info *info)
{
	u8 value;
	int ret;
	bool power_path_enabled = true;
	dev_err(info->dev, "find sc8989x_charger_get_power_path_status\n");

	ret = sc8989x_read(info, SC8989X_REG_0, &value);
	if (ret < 0) {
		dev_err(info->dev, "Fail to get power path status, ret = %d\n", ret);
		return power_path_enabled;
	}

	if (value & SC8989X_REG_EN_HIZ_MASK)
		power_path_enabled = false;

	return power_path_enabled;
}

static int sc8989x_charger_set_power_path_status(struct sc8989x_charger_info *info, bool enable)
{
	int ret = 0;
	u8 value = 0x1;

	if (enable)
		value = 0;
	dev_err(info->dev, "find sc8989x_charger_set_power_path_status\n");

	ret = sc8989x_update_bits(info, SC8989X_REG_0,
				  SC8989X_REG_EN_HIZ_MASK,
				  value << SC8989X_REG_EN_HIZ_SHIFT);
	if (ret)
		dev_err(info->dev, "%s HIZ mode failed, ret = %d\n",
			enable ? "Enable" : "Disable", ret);

	return ret;
}

static int sc8989x_charger_check_power_path_status(struct sc8989x_charger_info *info)
{
	int ret = 0;
	dev_err(info->dev, "find sc8989x_charger_check_power_path_status\n");

	if (info->disable_power_path)
		return 0;

	if (sc8989x_charger_get_power_path_status(info))
		return 0;

	dev_info(info->dev, "%s:line%d, disable HIZ\n", __func__, __LINE__);

	ret = sc8989x_update_bits(info, SC8989X_REG_0,
				  SC8989X_REG_EN_HIZ_MASK, 0);
	if (ret)
		dev_err(info->dev, "disable HIZ mode failed, ret = %d\n", ret);

	return ret;
}
#endif

static void sc8989x_check_wireless_charge(struct sc8989x_charger_info *info, bool enable)
{
	int ret;
	
  	dev_err(info->dev, "find sc8989x_check_wireless_charge\n");

    if (!enable)
        cancel_delayed_work_sync(&info->cur_work);

    if (info->is_wireless_charge && enable) {
        cancel_delayed_work_sync(&info->cur_work);
        ret = sc8989x_charger_set_current(info, info->current_charge_limit_cur);
        if (ret < 0)
            dev_err(info->dev, "%s:set charge current failed\n", __func__);

        ret = sc8989x_charger_set_current(info, info->current_input_limit_cur);
        if (ret < 0)
            dev_err(info->dev, "%s:set charge current failed\n", __func__);

        pm_wakeup_event(info->dev, SC8989X_WAKE_UP_MS);
        schedule_delayed_work(&info->cur_work, SC8989X_CURRENT_WORK_MS);
    } else if (info->is_wireless_charge && !enable) {
        info->new_charge_limit_cur = info->current_charge_limit_cur;
        info->current_charge_limit_cur = REG04_ICC_LSB * 1000;
        info->new_input_limit_cur = info->current_input_limit_cur;
        info->current_input_limit_cur = REG00_IINDPM_LSB * 1000;
    } else if (!info->is_wireless_charge && !enable) {
        info->new_charge_limit_cur = REG04_ICC_LSB * 1000;
        info->current_charge_limit_cur = REG04_ICC_LSB * 1000;
        info->new_input_limit_cur = REG00_IINDPM_LSB * 1000;
        info->current_input_limit_cur = REG00_IINDPM_LSB * 1000;
    }
}

static int sc8989x_charger_set_status(struct sc8989x_charger_info *info,
				      int val, u32 input_vol, bool bat_present)
{
	int ret = 0;
	dev_err(info->dev, "sc8989x_charger_set_status entry\n");
    if (val == CM_FAST_CHARGE_OVP_ENABLE_CMD) {
        ret = sc8989x_charger_set_fchg_current(info, val);
        if (ret) {
            dev_err(info->dev, "failed to set 9V fast charge current\n");
            return ret;
        }
        ret = sc8989x_charger_set_ovp(info, SC8989X_FCHG_OVP_9V);
        if (ret) {
            dev_err(info->dev, "failed to set fast charge 9V ovp\n");
            return ret;
        }
    } else if (val == CM_FAST_CHARGE_OVP_DISABLE_CMD) {
        ret = sc8989x_charger_set_fchg_current(info, val);
        if (ret) {
            dev_err(info->dev, "failed to set 5V normal charge current\n");
            return ret;
        }
        ret = sc8989x_charger_set_ovp(info, SC8989X_FCHG_OVP_6V);
        if (ret) {
            dev_err(info->dev, "failed to set fast charge 5V ovp\n");
            return ret;
        }
        if (info->role == SC8989X_ROLE_MASTER_DEFAULT) {
            if (input_vol > SC8989X_FAST_CHARGER_VOLTAGE_MAX)
                info->need_disable_Q1 = true;
        }
    } else if ((val == false) &&
        (info->role == SC8989X_ROLE_MASTER_DEFAULT)) {
        if (input_vol > SC8989X_NORMAL_CHARGER_VOLTAGE_MAX)
            info->need_disable_Q1 = true;
    }

    if (val > CM_FAST_CHARGE_NORMAL_CMD)
        return 0;

    if (!val && info->charging) {
        sc8989x_check_wireless_charge(info, false);
        sc8989x_charger_stop_charge(info, bat_present);
        sc8989x_dump_register(info);
        info->charging = false;
        pr_err("%s:line info->charging = false val->intval =%d \n", __func__, val);
    } else if (val && !info->charging) {
        sc8989x_check_wireless_charge(info, true);
        ret = sc8989x_charger_start_charge(info);
        sc8989x_dump_register(info);
        if (ret)
            dev_err(info->dev, "start charge failed\n");
        else
            info->charging = true;
        pr_err("%s:line info->charging = true val->intval =%d \n", __func__, val);
    }

    return ret;
}

static void sc8989x_current_work(struct work_struct *data)
{
	struct delayed_work *dwork = to_delayed_work(data);
    struct sc8989x_charger_info *info =
        container_of(dwork, struct sc8989x_charger_info, cur_work);
    int ret = 0;
    bool need_return = false;

  	dev_err(info->dev, "find sc8989x_current_work\n");

    if (!info) {
        pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
        return;
    }

    if (info->current_charge_limit_cur > info->new_charge_limit_cur) {
        ret = sc8989x_charger_set_current(info, info->new_charge_limit_cur);
        if (ret < 0)
            dev_err(info->dev, "%s: set charge limit cur failed\n", __func__);
        return;
    }

    if (info->current_input_limit_cur > info->new_input_limit_cur) {
        ret = sc8989x_charger_set_limit_current(info, info->new_input_limit_cur, false);
        if (ret < 0)
            dev_err(info->dev, "%s: set input limit cur failed\n", __func__);
        return;
    }

    if (info->current_charge_limit_cur + REG04_ICC_LSB * 1000 <=
        info->new_charge_limit_cur)
        info->current_charge_limit_cur += REG04_ICC_LSB * 1000;
    else
        need_return = true;

    if (info->current_input_limit_cur + REG00_IINDPM_LSB * 1000 <=
        info->new_input_limit_cur)
        info->current_input_limit_cur += REG00_IINDPM_LSB * 1000;
    else if (need_return)
        return;

    ret = sc8989x_charger_set_current(info, info->current_charge_limit_cur);
    if (ret < 0) {
        dev_err(info->dev, "set charge limit current failed\n");
        return;
    }

    ret = sc8989x_charger_set_limit_current(info, info->current_input_limit_cur, false);
    if (ret < 0) {
        dev_err(info->dev, "set input limit current failed\n");
        return;
    }
    sc8989x_dump_register(info);
    dev_info(info->dev, "set charge_limit_cur %duA, input_limit_curr %duA\n",
        info->current_charge_limit_cur, info->current_input_limit_cur);
    schedule_delayed_work(&info->cur_work, SC8989X_CURRENT_WORK_MS);
}

#if 0
static bool sc8989x_probe_is_ready(struct sc8989x_charger_info *info)
{
	unsigned long timeout;

	if (unlikely(!info->probe_initialized)) {
		timeout = wait_for_completion_timeout(&info->probe_init, SC8989X_PROBE_TIMEOUT);
		if (!timeout) {
			dev_err(info->dev, "%s wait probe timeout\n", __func__);
			return false;
		}
	}

	return true;
}
#endif

static enum power_supply_property sc8989x_usb_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_CALIBRATE,
	POWER_SUPPLY_PROP_TYPE,
};

static int sc8989x_charger_usb_get_property(struct power_supply *psy,
					    enum power_supply_property psp,
					    union power_supply_propval *val)
{
	struct sc8989x_charger_info *info = power_supply_get_drvdata(psy);
	u32 cur = 0, health, enabled = 0;
	int ret = 0;
  	dev_err(info->dev, "find sc8989x_charger_usb_get_property\n");


	if (!info) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return -EINVAL;
	}

	/*if (!sc8989x_probe_is_ready(info)) {
		dev_err(info->dev, "%s wait probe timeout\n", __func__);
		return -EINVAL;
	}*/

	mutex_lock(&info->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (val->intval == CM_POWER_PATH_ENABLE_CMD ||
		    val->intval == CM_POWER_PATH_DISABLE_CMD) {
			val->intval = sc8989x_get_hiz_status(info);
			break;
		}

		val->intval = sc8989x_charger_get_status(info);
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		if (!info->charging) {
			val->intval = 0;
		} else {
			ret = sc8989x_charger_get_current(info, &cur);
			if (ret)
				goto out;

			val->intval = cur;
		}
		break;

	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		if (!info->charging) {
			val->intval = 0;
		} else {
			ret = sc8989x_charger_get_limit_current(info, &cur);
			if (ret)
				goto out;

			val->intval = cur;
		}
		break;

	case POWER_SUPPLY_PROP_HEALTH:
		if (info->charging) {
            val->intval = 0;
        } else {
            ret = sc8989x_charger_get_health(info, &health);
            if (ret)
                goto out;

            val->intval = health;
        }
        break;

	case POWER_SUPPLY_PROP_CALIBRATE:
		if (info->role == SC8989X_ROLE_MASTER_DEFAULT) {
            ret = regmap_read(info->pmic, info->charger_pd, &enabled);
            if (ret) {
                dev_err(info->dev, "get sc8989x charge status failed\n");
                goto out;
            }
            val->intval = !(enabled & info->charger_pd_mask);
        } else if (info->role == SC8989X_ROLE_SLAVE) {
            enabled = gpiod_get_value_cansleep(info->gpiod);
            val->intval = !enabled;
        }

		break;
	default:
		ret = -EINVAL;
	}

out:
	mutex_unlock(&info->lock);
	return ret;
}

static int sc8989x_charger_usb_set_property(struct power_supply *psy,
					    enum power_supply_property psp,
					    const union power_supply_propval *val)
{
	struct sc8989x_charger_info *info = power_supply_get_drvdata(psy);
	int ret = 0;
	u32 input_vol;
	bool bat_present;
  	dev_err(info->dev, "find sc8989x_charger_usb_set_property\n");

	if (!info) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return -EINVAL;
	}

	/*
	 * input_vol and bat_present should be assigned a value, only if psp is
	 * POWER_SUPPLY_PROP_STATUS and POWER_SUPPLY_PROP_CALIBRATE.
	 */
	if (psp == POWER_SUPPLY_PROP_STATUS || psp == POWER_SUPPLY_PROP_CALIBRATE) {
		bat_present = sc8989x_charger_is_bat_present(info);
		ret = sc8989x_charger_get_charge_voltage(info, &input_vol);
		if (ret) {
			input_vol = 0;
			dev_err(info->dev, "failed to get charge voltage! ret = %d\n", ret);
		}
	}

	mutex_lock(&info->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		if (info->is_wireless_charge) {
            cancel_delayed_work_sync(&info->cur_work);
            info->new_charge_limit_cur = val->intval;
            pm_wakeup_event(info->dev, SC8989X_WAKE_UP_MS);
            schedule_delayed_work(&info->cur_work, SC8989X_CURRENT_WORK_MS * 2);
            break;
        }

        ret = sc8989x_charger_set_current(info, val->intval);
        if (ret < 0)
            dev_err(info->dev, "set charge current failed\n");
        break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		if (info->is_wireless_charge) {
            cancel_delayed_work_sync(&info->cur_work);
            info->new_input_limit_cur = val->intval;
            pm_wakeup_event(info->dev, SC8989X_WAKE_UP_MS);
            schedule_delayed_work(&info->cur_work, SC8989X_CURRENT_WORK_MS * 2);
            break;
        }
	
        ret = sc8989x_charger_set_limit_current(info, val->intval, false);
        if (ret < 0)
            dev_err(info->dev, "set input current limit failed\n");
        break;
	case POWER_SUPPLY_PROP_STATUS:
		if (val->intval == CM_POWER_PATH_ENABLE_CMD) {
            sc8989x_exit_hiz_mode(info);
            break;
        } else if (val->intval == CM_POWER_PATH_DISABLE_CMD) {
            sc8989x_enter_hiz_mode(info);
            break;
        }
        dev_err(info->dev, "charger status val->intval = %d , input_vol = %d , bat_present = %d\n",val->intval,input_vol,bat_present);
        ret = sc8989x_charger_set_status(info, val->intval, input_vol, bat_present);
        if (ret < 0)
            dev_err(info->dev, "set charge status failed\n");
        break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		ret = sc8989x_charger_set_termina_vol(info, val->intval / 1000);
		if (ret < 0)
			dev_err(info->dev, "failed to set terminate voltage\n");
		break;

	case POWER_SUPPLY_PROP_CALIBRATE:
		if (val->intval == true) {
            sc8989x_check_wireless_charge(info, true);
            ret = sc8989x_charger_start_charge(info);
            sc8989x_dump_register(info);
            if (ret)
                dev_err(info->dev, "start charge failed\n");
            else
                info->charging = true;
        } else if (val->intval == false) {
            sc8989x_check_wireless_charge(info, false);
            sc8989x_charger_stop_charge(info, bat_present);
            sc8989x_dump_register(info);
            info->charging = false;
        }
        break;
	case POWER_SUPPLY_PROP_TYPE:
		if (val->intval == POWER_SUPPLY_WIRELESS_CHARGER_TYPE_BPP) {
            info->is_wireless_charge = true;
            ret = sc8989x_charger_set_ovp(info, SC8989X_FCHG_OVP_6V);
        } else if (val->intval == POWER_SUPPLY_WIRELESS_CHARGER_TYPE_EPP) {
            info->is_wireless_charge = true;
            ret = sc8989x_charger_set_ovp(info, SC8989X_FCHG_OVP_14V);
        } else {
            info->is_wireless_charge = false;
            ret = sc8989x_charger_set_ovp(info, SC8989X_FCHG_OVP_6V);
        }
        if (ret)
            dev_err(info->dev, "failed to set fast charge ovp\n");

        break;
	case POWER_SUPPLY_PROP_PRESENT:
		info->is_charger_online = val->intval;
		if (val->intval == true) {
			info->last_wdt_time = ktime_to_ms(ktime_get());
			schedule_delayed_work(&info->wdt_work, 0);
		} else {
			info->actual_limit_cur = 0;
			cancel_delayed_work_sync(&info->wdt_work);
		}
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&info->lock);
	return ret;
}

static int sc8989x_charger_property_is_writeable(struct power_supply *psy,
						 enum power_supply_property psp)
{
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
	case POWER_SUPPLY_PROP_CALIBRATE:
	case POWER_SUPPLY_PROP_TYPE:
	case POWER_SUPPLY_PROP_STATUS:
	case POWER_SUPPLY_PROP_PRESENT:
		ret = 1;
		break;

	default:
		ret = 0;
	}

	return ret;
}

static const struct power_supply_desc sc8989x_charger_desc = {
	.name			= "sc8989x_charger",
	.type			= POWER_SUPPLY_TYPE_UNKNOWN,
	.properties		= sc8989x_usb_props,
	.num_properties		= ARRAY_SIZE(sc8989x_usb_props),
	.get_property		= sc8989x_charger_usb_get_property,
	.set_property		= sc8989x_charger_usb_set_property,
	.property_is_writeable	= sc8989x_charger_property_is_writeable,
};

static const struct power_supply_desc sc8989x_slave_charger_desc = {
	.name			= "sc8989x_slave_charger",
	.type			= POWER_SUPPLY_TYPE_UNKNOWN,
	.properties		= sc8989x_usb_props,
	.num_properties		= ARRAY_SIZE(sc8989x_usb_props),
	.get_property		= sc8989x_charger_usb_get_property,
	.set_property		= sc8989x_charger_usb_set_property,
	.property_is_writeable	= sc8989x_charger_property_is_writeable,
};

static ssize_t sc8989x_register_value_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct sc8989x_charger_sysfs *sc8989x_sysfs =
        container_of(attr, struct sc8989x_charger_sysfs,
                attr_sc8989x_reg_val);
    struct  sc8989x_charger_info *info =  sc8989x_sysfs->info;
    u8 val;
    int ret;

    if (!info)
        return snprintf(buf, PAGE_SIZE, "%s  sc8989x_sysfs->info is null\n", __func__);

    ret = sc8989x_read(info, reg_tab[info->reg_id].addr, &val);
    if (ret) {
        dev_err(info->dev, "fail to get  SC8989X_REG_0x%.2x value, ret = %d\n",
            reg_tab[info->reg_id].addr, ret);
        return snprintf(buf, PAGE_SIZE, "fail to get  SC8989X_REG_0x%.2x value\n",
                reg_tab[info->reg_id].addr);
    }

    return snprintf(buf, PAGE_SIZE, "SC8989X_REG_0x%.2x = 0x%.2x\n",
            reg_tab[info->reg_id].addr, val);
}

static ssize_t sc8989x_register_value_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t count)
{
	struct sc8989x_charger_sysfs *sc8989x_sysfs =
        container_of(attr, struct sc8989x_charger_sysfs,
                attr_sc8989x_reg_val);
    struct sc8989x_charger_info *info = sc8989x_sysfs->info;
    u8 val;
    int ret;

    if (!info) {
        dev_err(dev, "%s sc8989x_sysfs->info is null\n", __func__);
        return count;
    }

    ret =  kstrtou8(buf, 16, &val);
    if (ret) {
        dev_err(info->dev, "fail to get addr, ret = %d\n", ret);
        return count;
    }

    ret = sc8989x_write(info, reg_tab[info->reg_id].addr, val);
    if (ret) {
        dev_err(info->dev, "fail to wite 0x%.2x to REG_0x%.2x, ret = %d\n",
                val, reg_tab[info->reg_id].addr, ret);
        return count;
    }

    dev_info(info->dev, "wite 0x%.2x to REG_0x%.2x success\n", val, reg_tab[info->reg_id].addr);
    return count;
}

static ssize_t sc8989x_register_id_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct sc8989x_charger_sysfs *sc8989x_sysfs =
        container_of(attr, struct sc8989x_charger_sysfs,
                attr_sc8989x_sel_reg_id);
    struct sc8989x_charger_info *info = sc8989x_sysfs->info;
    int ret, id;

    if (!info) {
        dev_err(dev, "%s sc8989x_sysfs->info is null\n", __func__);
        return count;
    }

    ret =  kstrtoint(buf, 10, &id);
    if (ret) {
        dev_err(info->dev, "%s store register id fail\n", sc8989x_sysfs->name);
        return count;
    }

    if (id < 0 || id >= SC8989X_REG_NUM) {
        dev_err(info->dev, "%s store register id fail, id = %d is out of range\n",
            sc8989x_sysfs->name, id);
        return count;
    }

    info->reg_id = id;

    dev_info(info->dev, "%s store register id = %d success\n", sc8989x_sysfs->name, id);
    return count;
}

static ssize_t sc8989x_register_id_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct sc8989x_charger_sysfs *sc8989x_sysfs =
        container_of(attr, struct sc8989x_charger_sysfs,
                attr_sc8989x_sel_reg_id);
    struct sc8989x_charger_info *info = sc8989x_sysfs->info;

    if (!info)
        return snprintf(buf, PAGE_SIZE, "%s sc8989x_sysfs->info is null\n", __func__);

    return snprintf(buf, PAGE_SIZE, "Curent register id = %d\n", info->reg_id);
}

static ssize_t sc8989x_register_batfet_store(struct device *dev,
                    struct device_attribute *attr,
                    const char *buf, size_t count)
{
    struct sc8989x_charger_sysfs *sc8989x_sysfs =
    container_of(attr, struct sc8989x_charger_sysfs, attr_sc8989x_batfet_val);
    struct sc8989x_charger_info *info = sc8989x_sysfs->info;
    int ret;
    bool batfet;

    if (!info) {
        dev_err(dev, "%s sc8989x_sysfs->info is null\n", __func__);
        return count;
    }

    ret =  kstrtobool(buf, &batfet);
    if (ret) {
        dev_err(info->dev, "batfet fail\n");
        return count;
    }

    if (batfet) {
        ret = sc8989x_update_bits(info, SC8989X_REG_9, REG09_BATFET_DIS_MASK, 
                REG09_BATFET_DISABLE << REG09_BATFET_DIS_SHIFT);
        if (ret)
            dev_err(info->dev, "enter batfet mode failed\n");
    } else {
        ret = sc8989x_update_bits(info, SC8989X_REG_9, REG09_BATFET_DIS_MASK, 
                REG09_BATFET_ENABLE << REG09_BATFET_DIS_SHIFT);
        if (ret)
            dev_err(info->dev, "exit batfet mode failed\n");
    }
    return count;
}

static ssize_t sc8989x_register_batfet_show(struct device *dev,
                    struct device_attribute *attr,
                    char *buf)
{    u8 batfet , value;
    int ret;
    struct sc8989x_charger_sysfs *sc8989x_sysfs =
        container_of(attr, struct sc8989x_charger_sysfs,
                attr_sc8989x_batfet_val);
    struct sc8989x_charger_info *info = sc8989x_sysfs->info;

    if (!info)
        return snprintf(buf, PAGE_SIZE, "%s sc8989x_sysfs->info is null\n", __func__);
    ret = sc8989x_read(info, SC8989X_REG_9, &batfet);
    value = (batfet & REG09_BATFET_DIS_MASK) >> REG09_BATFET_DIS_SHIFT;
    return sprintf(buf, "%d\n", value);
}

static ssize_t sc8989x_register_hizi_store(struct device *dev,
                    struct device_attribute *attr,
                    const char *buf, size_t count)
{
    struct sc8989x_charger_sysfs *sc8989x_sysfs =
        container_of(attr, struct sc8989x_charger_sysfs,
                attr_sc8989x_hizi_val);
    struct sc8989x_charger_info *info = sc8989x_sysfs->info;
    int ret;
    bool batfet;

    if (!info) {
        dev_err(dev, "%s sc8989x_sysfs->info is null\n", __func__);
        return count;
    }

    ret =  kstrtobool(buf, &batfet);
    if (ret) {
        dev_err(info->dev, "hizi_store fail\n");
        return count;
    }

    if (batfet) {
        ret = sc8989x_enter_hiz_mode(info);
        if (ret)
            dev_err(info->dev, "enter HIZ mode failed\n");
    } else {
        ret = sc8989x_exit_hiz_mode(info);
        if (ret)
            dev_err(info->dev, "exit HIZ mode failed\n");
    }
    return count;
}

static ssize_t sc8989x_register_hizi_show(struct device *dev,
                    struct device_attribute *attr,
                    char *buf)
{    u8 batfet , value;
    int ret;
    struct sc8989x_charger_sysfs *sc8989x_sysfs =
    container_of(attr, struct sc8989x_charger_sysfs, attr_sc8989x_hizi_val);
    struct sc8989x_charger_info *info = sc8989x_sysfs->info;

    if (!info)
        return snprintf(buf, PAGE_SIZE, "%s sc8989x_sysfs->info is null\n", __func__);
    ret = sc8989x_read(info, SC8989X_REG_0, &batfet);
    value = (batfet & REG00_EN_HIZ_MASK) >> REG00_EN_HIZ_SHIFT;
    return sprintf(buf, "%d\n", value);
}

static ssize_t sc8989x_register_table_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct sc8989x_charger_sysfs *sc8989x_sysfs =
    container_of(attr, struct sc8989x_charger_sysfs, attr_sc8989x_lookup_reg);
    struct sc8989x_charger_info *info = sc8989x_sysfs->info;
    int i, len, idx = 0;
    char reg_tab_buf[2048];

    if (!info)
        return snprintf(buf, PAGE_SIZE, "%s sc8989x_sysfs->info is null\n", __func__);

    memset(reg_tab_buf, '\0', sizeof(reg_tab_buf));
    len = snprintf(reg_tab_buf + idx, sizeof(reg_tab_buf) - idx,
            "Format: [id] [addr] [desc]\n");
    idx += len;

    for (i = 0; i < SC8989X_REG_NUM; i++) {
        len = snprintf(reg_tab_buf + idx, sizeof(reg_tab_buf) - idx,
                "[%d] [REG_0x%.2x] [%s]; \n",
                reg_tab[i].id, reg_tab[i].addr, reg_tab[i].name);
        idx += len;
    }

    return snprintf(buf, PAGE_SIZE, "%s\n", reg_tab_buf);
}

static ssize_t sc8989x_dump_register_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct sc8989x_charger_sysfs *sc8989x_sysfs =
        container_of(attr, struct sc8989x_charger_sysfs,
                attr_sc8989x_dump_reg);
    struct sc8989x_charger_info *info = sc8989x_sysfs->info;

    if (!info)
        return snprintf(buf, PAGE_SIZE, "%s sc8989x_sysfs->info is null\n", __func__);

    sc8989x_dump_register(info);

    return snprintf(buf, PAGE_SIZE, "%s\n", sc8989x_sysfs->name);
}

static int sc8989x_register_sysfs(struct sc8989x_charger_info *info)
{
	struct sc8989x_charger_sysfs *sc8989x_sysfs;
    int ret;

    sc8989x_sysfs = devm_kzalloc(info->dev, sizeof(*sc8989x_sysfs), GFP_KERNEL);
    if (!sc8989x_sysfs)
        return -ENOMEM;

    info->sysfs = sc8989x_sysfs;
    sc8989x_sysfs->name = "sc8989x_sysfs";
    sc8989x_sysfs->info = info;
    sc8989x_sysfs->attrs[0] = &sc8989x_sysfs->attr_sc8989x_dump_reg.attr;
    sc8989x_sysfs->attrs[1] = &sc8989x_sysfs->attr_sc8989x_lookup_reg.attr;
    sc8989x_sysfs->attrs[2] = &sc8989x_sysfs->attr_sc8989x_sel_reg_id.attr;
    sc8989x_sysfs->attrs[3] = &sc8989x_sysfs->attr_sc8989x_reg_val.attr;
    sc8989x_sysfs->attrs[4] = &sc8989x_sysfs->attr_sc8989x_batfet_val.attr;
    sc8989x_sysfs->attrs[5] = &sc8989x_sysfs->attr_sc8989x_hizi_val.attr;
    sc8989x_sysfs->attrs[6] = NULL;
    sc8989x_sysfs->attr_g.name = "debug";
    sc8989x_sysfs->attr_g.attrs = sc8989x_sysfs->attrs;

    sysfs_attr_init(&sc8989x_sysfs->attr_sc8989x_dump_reg.attr);
    sc8989x_sysfs->attr_sc8989x_dump_reg.attr.name = "sc8989x_dump_reg";
    sc8989x_sysfs->attr_sc8989x_dump_reg.attr.mode = 0444;
    sc8989x_sysfs->attr_sc8989x_dump_reg.show = sc8989x_dump_register_show;

    sysfs_attr_init(&sc8989x_sysfs->attr_sc8989x_lookup_reg.attr);
    sc8989x_sysfs->attr_sc8989x_lookup_reg.attr.name = "sc8989x_lookup_reg";
    sc8989x_sysfs->attr_sc8989x_lookup_reg.attr.mode = 0444;
    sc8989x_sysfs->attr_sc8989x_lookup_reg.show = sc8989x_register_table_show;

    sysfs_attr_init(&sc8989x_sysfs->attr_sc8989x_sel_reg_id.attr);
    sc8989x_sysfs->attr_sc8989x_sel_reg_id.attr.name = "sc8989x_sel_reg_id";
    sc8989x_sysfs->attr_sc8989x_sel_reg_id.attr.mode = 0644;
    sc8989x_sysfs->attr_sc8989x_sel_reg_id.show = sc8989x_register_id_show;
    sc8989x_sysfs->attr_sc8989x_sel_reg_id.store = sc8989x_register_id_store;

    sysfs_attr_init(&sc8989x_sysfs->attr_sc8989x_reg_val.attr);
    sc8989x_sysfs->attr_sc8989x_reg_val.attr.name = "sc8989x_reg_val";
    sc8989x_sysfs->attr_sc8989x_reg_val.attr.mode = 0644;
    sc8989x_sysfs->attr_sc8989x_reg_val.show = sc8989x_register_value_show;
    sc8989x_sysfs->attr_sc8989x_reg_val.store = sc8989x_register_value_store;

    sysfs_attr_init(&sc8989x_sysfs->attr_sc8989x_batfet_val.attr);
    sc8989x_sysfs->attr_sc8989x_batfet_val.attr.name = "charger_batfet_val";
    sc8989x_sysfs->attr_sc8989x_batfet_val.attr.mode = 0644;
    sc8989x_sysfs->attr_sc8989x_batfet_val.show = sc8989x_register_batfet_show;
    sc8989x_sysfs->attr_sc8989x_batfet_val.store = sc8989x_register_batfet_store;

    sysfs_attr_init(&sc8989x_sysfs->attr_sc8989x_batfet_val.attr);
    sc8989x_sysfs->attr_sc8989x_hizi_val.attr.name = "charger_hizi_val";
    sc8989x_sysfs->attr_sc8989x_hizi_val.attr.mode = 0644;
    sc8989x_sysfs->attr_sc8989x_hizi_val.show = sc8989x_register_hizi_show;
    sc8989x_sysfs->attr_sc8989x_hizi_val.store = sc8989x_register_hizi_store;

    ret = sysfs_create_group(&info->psy_usb->dev.kobj, &sc8989x_sysfs->attr_g);
    if (ret < 0)
        dev_err(info->dev, "Cannot create sysfs , ret = %d\n", ret);

    return ret;
}

static void
sc8989x_charger_feed_watchdog_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
    struct sc8989x_charger_info *info = container_of(dwork,
                            struct sc8989x_charger_info,
                            wdt_work);
    int ret;

    ret = sc8989x_update_bits(info, SC8989X_REG_3,
                REG03_WD_RST_MASK, REG03_WD_RST_MASK);
    if (ret) {
        dev_err(info->dev, "reset sc8989x failed\n");
        return;
    }
    schedule_delayed_work(&info->wdt_work, HZ * 50);
}

#if IS_ENABLED(CONFIG_REGULATOR)
static bool sc8989x_charger_check_otg_valid(struct sc8989x_charger_info *info)
{
	int ret;
    u8 value = 0;
    bool status = false;

    ret = sc8989x_read(info, SC8989X_REG_3, &value);
    if (ret) {
        dev_err(info->dev, "get sc8989x charger otg valid status failed\n");
        return status;
    }

    if (value & REG03_OTG_MASK)
        status = true;
    else
        dev_err(info->dev, "otg is not valid, REG_1 = 0x%x\n", value);

    return status;
}

static bool sc8989x_charger_check_otg_fault(struct sc8989x_charger_info *info)
{
	int ret;
    u8 value = 0;
    bool status = true;

    ret = sc8989x_read(info, SC8989X_REG_C, &value);
    if (ret) {
        dev_err(info->dev, "get sc8989x charger otg fault status failed\n");
        return status;
    }

    if (!(value & REG0C_OTG_FAULT))
        status = false;
    else
        dev_err(info->dev, "boost fault occurs, REG_9 = 0x%x\n", value);

    return status;
}

static void sc8989x_charger_otg_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
    struct sc8989x_charger_info *info = container_of(dwork,
            struct sc8989x_charger_info, otg_work);
    bool otg_valid = sc8989x_charger_check_otg_valid(info);
    bool otg_fault;
    int ret, retry = 0;

    if (otg_valid)
        goto out;

    do {
        otg_fault = sc8989x_charger_check_otg_fault(info);
        if (!otg_fault) {
            ret = sc8989x_charger_set_otg_en(info, true);
            if (ret)
                dev_err(info->dev, "restart sc8989x charger otg failed\n");
            ret = sc8989x_charger_set_chg_en(info, false);
            if (ret)
                dev_err(info->dev, "disable sc8989x charger failed\n");
        }

        otg_valid = sc8989x_charger_check_otg_valid(info);
    } while (!otg_valid && retry++ < SC8989X_OTG_RETRY_TIMES);

    if (retry >= SC8989X_OTG_RETRY_TIMES) {
        dev_err(info->dev, "Restart OTG failed\n");
        return;
    }

out:
    schedule_delayed_work(&info->otg_work, msecs_to_jiffies(1500));
}

static int sc8989x_charger_enable_otg(struct regulator_dev *dev)
{
	struct sc8989x_charger_info *info = rdev_get_drvdata(dev);
    int ret = 0;

    dev_info(info->dev, "%s:line%d enter\n", __func__, __LINE__);
    if (!info) {
        pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
        return -EINVAL;
    }

    mutex_lock(&info->lock);

    /*
    * Disable charger detection function in case
    * affecting the OTG timing sequence.
    */
    if (!info->use_typec_extcon) {
        ret = regmap_update_bits(info->pmic, info->charger_detect,
                    BIT_DP_DM_BC_ENB, BIT_DP_DM_BC_ENB);
        if (ret) {
            dev_err(info->dev, "failed to disable bc1.2 detect function.\n");
            goto out;
        }
    }

    ret = sc8989x_charger_set_chg_en(info, false);
    if (ret)
        dev_err(info->dev, "disable sc8989x charger failed\n");

    ret = sc8989x_charger_set_otg_en(info, true);
    if (ret) {
        dev_err(info->dev, "enable sc8989x otg failed\n");
        regmap_update_bits(info->pmic, info->charger_detect,
                BIT_DP_DM_BC_ENB, 0);
        goto out;
    }

    info->otg_enable = true;
    schedule_delayed_work(&info->wdt_work,
                msecs_to_jiffies(SC8989X_FEED_WATCHDOG_VALID_MS));
    schedule_delayed_work(&info->otg_work,
                msecs_to_jiffies(SC8989X_OTG_VALID_MS));
out:
    mutex_unlock(&info->lock);
    return ret;
}

static int sc8989x_charger_disable_otg(struct regulator_dev *dev)
{
	struct sc8989x_charger_info *info = rdev_get_drvdata(dev);
    int ret = 0;

    dev_info(info->dev, "%s:line%d enter\n", __func__, __LINE__);
    if (!info) {
        pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
        return -EINVAL;
    }

    mutex_lock(&info->lock);

    info->otg_enable = false;
    cancel_delayed_work_sync(&info->wdt_work);
    cancel_delayed_work_sync(&info->otg_work);
    ret = sc8989x_charger_set_otg_en(info, false);
    if (ret) {
        dev_err(info->dev, "disable sc8989x otg failed\n");
        goto out;
    }

    if (!info->use_typec_extcon) {
        ret = regmap_update_bits(info->pmic, info->charger_detect, BIT_DP_DM_BC_ENB, 0);
        if (ret)
            dev_err(info->dev, "enable BC1.2 failed\n");
    }

out:
    mutex_unlock(&info->lock);
    return ret;

}

static int sc8989x_charger_vbus_is_enabled(struct regulator_dev *dev)
{
	struct sc8989x_charger_info *info = rdev_get_drvdata(dev);
    int ret;
    u8 val;

    dev_info(info->dev, "%s:line%d enter\n", __func__, __LINE__);
    if (!info) {
        pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
        return -EINVAL;
    }

    mutex_lock(&info->lock);

    ret = sc8989x_read(info, SC8989X_REG_3, &val);
    if (ret) {
        dev_err(info->dev, "failed to get sc8989x otg status\n");
        mutex_unlock(&info->lock);
        return ret;
    }

    val &= REG03_OTG_MASK;
    dev_info(info->dev, "%s:line%d val = %d\n", __func__, __LINE__, val);
    
    mutex_unlock(&info->lock);
    return val;
}

static const struct regulator_ops sc8989x_charger_vbus_ops = {
	.enable = sc8989x_charger_enable_otg,
	.disable = sc8989x_charger_disable_otg,
	.is_enabled = sc8989x_charger_vbus_is_enabled,
};

static const struct regulator_desc sc8989x_charger_vbus_desc = {
	.name = "otg-vbus",
	.of_match = "otg-vbus",
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
	.ops = &sc8989x_charger_vbus_ops,
	.fixed_uV = 5000000,
	.n_voltages = 1,
};

static int
sc8989x_charger_register_vbus_regulator(struct sc8989x_charger_info *info)
{
	struct regulator_config cfg = { };
    struct regulator_dev *reg;
    int ret = 0;

    /*
	 * only master to support otg
	 */
	if (info->role != SC8989X_ROLE_MASTER_DEFAULT)
		return 0;


    cfg.dev = info->dev;
    cfg.driver_data = info;
    reg = devm_regulator_register(info->dev,
                    &sc8989x_charger_vbus_desc, &cfg);
    if (IS_ERR(reg)) {
        ret = PTR_ERR(reg);
        dev_err(info->dev, "Can't register regulator:%d\n", ret);
    }

    return ret;
}

static int sc8989x_charger_register_external_vbus_regulator(struct sc8989x_charger_info *info)
{
	struct regulator_config cfg = { };
	struct regulator_dev *reg;
	int ret = 0;
	struct device_node *otg_nd;
	struct device_node *otg_parent_nd;
	struct platform_device *otg_parent_nd_pdev;

	/*
	 * only master to support otg
	 */
	if (info->role != SC8989X_ROLE_MASTER_DEFAULT)
		return 0;

	otg_nd = of_find_node_by_name(NULL, "otg-vbus");
	if (!otg_nd) {
		dev_warn(info->dev, "%s, unable to get otg node\n", __func__);
		return -EPROBE_DEFER;
	}

	otg_parent_nd = of_get_parent(otg_nd);
	of_node_put(otg_nd);
	if (!otg_parent_nd) {
		dev_warn(info->dev, "%s, unable to get otg parent node\n", __func__);
		return -EPROBE_DEFER;
	}

	otg_parent_nd_pdev = of_find_device_by_node(otg_parent_nd);
	of_node_put(otg_parent_nd);
	if (!otg_parent_nd_pdev) {
		dev_warn(info->dev, "%s, unable to get otg parent node device\n", __func__);
		return -EPROBE_DEFER;
	}

	cfg.dev = &otg_parent_nd_pdev->dev;
	platform_device_put(otg_parent_nd_pdev);
	cfg.driver_data = info;
	reg = devm_regulator_register(cfg.dev, &sc8989x_charger_vbus_desc, &cfg);
	if (IS_ERR(reg)) {
		ret = PTR_ERR(reg);
		dev_warn(info->dev, "%s, failed to register vddvbus regulator:%d\n",
			 __func__, ret);
 	}
        return ret;
}

#else
static int
sc8989x_charger_register_vbus_regulator(struct sc8989x_charger_info *info)
{
	return 0;
}
#endif

static int sc8989x_charger_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct device *dev = &client->dev;
	struct power_supply_config charger_cfg = { };
	struct sc8989x_charger_info *info;
	struct device_node *regmap_np;
	struct platform_device *regmap_pdev;
	int ret;

	if (!adapter) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(dev, "No support for SMBUS_BYTE_DATA\n");
		return -ENODEV;
	}

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->client = client;
	info->dev = dev;

    ret = sc8989x_update_bits(info, SC8989X_REG_0, 6, 0);

    ret = sc8989x_charger_get_vendor_id_part_value(info);
    if (ret) {
        dev_err(dev, "failed to get vendor id, part value\n");
        return ret;
    }

	i2c_set_clientdata(client, info);
	power_path_control(info);

	ret = sc8989x_charger_is_fgu_present(info);
	if (ret) {
		dev_err(dev, "sc27xx_fgu not ready.\n");
		return -EPROBE_DEFER;
	}

	info->use_typec_extcon = device_property_read_bool(dev, "use-typec-extcon");
	info->disable_wdg = device_property_read_bool(dev, "disable-otg-wdg-in-sleep");

	ret = device_property_read_bool(dev, "role-slave");
	if (ret)
		info->role = SC8989X_ROLE_SLAVE;
	else
		info->role = SC8989X_ROLE_MASTER_DEFAULT;

	if (info->role == SC8989X_ROLE_SLAVE) {
		info->gpiod = devm_gpiod_get(dev, "enable", GPIOD_OUT_HIGH);
		if (IS_ERR(info->gpiod)) {
			dev_err(dev, "failed to get enable gpio\n");
			return PTR_ERR(info->gpiod);
		}
	}

	regmap_np = of_find_compatible_node(NULL, NULL, "sprd,sc27xx-syscon");
	if (!regmap_np)
		regmap_np = of_find_compatible_node(NULL, NULL, "sprd,ump962x-syscon");

	if (regmap_np) {
		if (of_device_is_compatible(regmap_np->parent, "sprd,sc2721"))
			info->charger_pd_mask = SC8989X_DISABLE_PIN_MASK_2721;
		else
			info->charger_pd_mask = SC8989X_DISABLE_PIN_MASK;
	} else {
		dev_err(dev, "unable to get syscon node\n");
		return -ENODEV;
	}

	ret = of_property_read_u32_index(regmap_np, "reg", 1,
					 &info->charger_detect);
	if (ret) {
		dev_err(dev, "failed to get charger_detect\n");
		return -EINVAL;
	}

	ret = of_property_read_u32_index(regmap_np, "reg", 2,
					 &info->charger_pd);
	if (ret) {
		dev_err(dev, "failed to get charger_pd reg\n");
		return ret;
	}

	regmap_pdev = of_find_device_by_node(regmap_np);
	if (!regmap_pdev) {
		of_node_put(regmap_np);
		dev_err(dev, "unable to get syscon device\n");
		return -ENODEV;
	}

	of_node_put(regmap_np);
	info->pmic = dev_get_regmap(regmap_pdev->dev.parent, NULL);
	if (!info->pmic) {
		dev_err(dev, "unable to get pmic regmap device\n");
		return -ENODEV;
	}

	mutex_init(&info->lock);
	mutex_init(&info->input_limit_cur_lock);
	init_completion(&info->probe_init);

	charger_cfg.drv_data = info;
	charger_cfg.of_node = dev->of_node;
	if (info->role == SC8989X_ROLE_MASTER_DEFAULT) {
		info->psy_usb = devm_power_supply_register(dev,
							   &sc8989x_charger_desc,
							   &charger_cfg);
	} else if (info->role == SC8989X_ROLE_SLAVE) {
		info->psy_usb = devm_power_supply_register(dev,
							   &sc8989x_slave_charger_desc,
							   &charger_cfg);
	}

	if (IS_ERR(info->psy_usb)) {
		dev_err(dev, "failed to register power supply\n");
		ret = PTR_ERR(info->psy_usb);
		goto err_regmap_exit;
	}

	ret = sc8989x_charger_hw_init(info);
	if (ret) {
		dev_err(dev, "failed to sc8989x_charger_hw_init\n");
		goto err_psy_usb;
	}

	sc8989x_charger_stop_charge(info, true);
	sc8989x_dump_register(info);
  
	device_init_wakeup(info->dev, true);

	alarm_init(&info->otg_timer, ALARM_BOOTTIME, NULL);
	INIT_DELAYED_WORK(&info->otg_work, sc8989x_charger_otg_work);
	INIT_DELAYED_WORK(&info->wdt_work, sc8989x_charger_feed_watchdog_work);

	/*
	 * only master to support otg
	 */
	if (info->role == SC8989X_ROLE_MASTER_DEFAULT) {
        if (device_property_read_bool(dev, "otg-vbus-node-external"))
		    ret = sc8989x_charger_register_external_vbus_regulator(info);
	    else
            ret = sc8989x_charger_register_vbus_regulator(info);
	    if (ret) {
		    dev_err(dev, "failed to register vbus regulator.\n");
		    goto err_psy_usb;
        }
	}

	INIT_DELAYED_WORK(&info->cur_work, sc8989x_current_work);
  	INIT_DELAYED_WORK(&info->cur_work, sc8989x_dump_reg_work);

	ret = sc8989x_register_sysfs(info);
	if (ret) {
		dev_err(info->dev, "register sysfs fail, ret = %d\n", ret);
		goto error_sysfs;
	}

	info->irq_gpio = of_get_named_gpio(info->dev->of_node, "irq-gpio", 0);
	if (gpio_is_valid(info->irq_gpio)) {
		ret = devm_gpio_request_one(info->dev, info->irq_gpio,
					    GPIOF_DIR_IN, "sc8989x_int");
		if (!ret)
			info->client->irq = gpio_to_irq(info->irq_gpio);
		else
			dev_err(dev, "int request failed, ret = %d\n", ret);

		if (info->client->irq < 0) {
			dev_err(dev, "failed to get irq no\n");
			gpio_free(info->irq_gpio);
		} else {
			/*ret = devm_request_threaded_irq(&info->client->dev, info->client->irq,
							NULL, sc8989x_int_handler,
							IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
							"sc8989x interrupt", info);
			if (ret)
				dev_err(info->dev, "Failed irq = %d ret = %d\n",
					info->client->irq, ret);
			else
				enable_irq_wake(client->irq);*/
		}
	} else {
		dev_err(dev, "failed to get irq gpio\n");
	}

    dev_err(info->dev, "set boost cur 1.2A\n");
    ret = sc8989x_update_bits(info, SC8989X_REG_A, SC8989X_REG_BOOST_MASK, 0x2 << SC8989X_REG_BOOST_SHIFT); //set otg cur 1.2A
	info->probe_initialized = true;
	complete_all(&info->probe_init);

	sc8989x_dump_register(info);
	dev_info(dev, "use_typec_extcon = %d\n", info->use_typec_extcon);

	return 0;

error_sysfs:
	sysfs_remove_group(&info->psy_usb->dev.kobj, &info->sysfs->attr_g);
err_psy_usb:
    power_supply_unregister(info->psy_usb);
	if (info->irq_gpio)
		gpio_free(info->irq_gpio);
err_regmap_exit:
    regmap_exit(info->pmic);
	mutex_destroy(&info->input_limit_cur_lock);
	mutex_destroy(&info->lock);
	return ret;
}

static void sc8989x_charger_shutdown(struct i2c_client *client)
{
	struct sc8989x_charger_info *info = i2c_get_clientdata(client);
	int ret = 0;

	cancel_delayed_work_sync(&info->wdt_work);
	if (info->otg_enable) {
		info->otg_enable = false;
		cancel_delayed_work_sync(&info->otg_work);
		ret = sc8989x_update_bits(info, SC8989X_REG_3,
					  REG03_OTG_MASK,
					  0);
		if (ret)
			dev_err(info->dev, "disable sc8989x otg failed ret = %d\n", ret);

		/* Enable charger detection function to identify the charger type */
		ret = regmap_update_bits(info->pmic, info->charger_detect,
					 BIT_DP_DM_BC_ENB, 0);
		if (ret)
			dev_err(info->dev,
				"enable charger detection function failed ret = %d\n", ret);
	}
	info->shutdown_flag = true;
}

static int sc8989x_charger_remove(struct i2c_client *client)
{
	struct sc8989x_charger_info *info = i2c_get_clientdata(client);

	cancel_delayed_work_sync(&info->wdt_work);
	cancel_delayed_work_sync(&info->otg_work);

	mutex_destroy(&info->input_limit_cur_lock);
	mutex_destroy(&info->lock);

	return 0;
}

#if IS_ENABLED(CONFIG_PM_SLEEP)
static int sc8989x_charger_suspend(struct device *dev)
{
	struct sc8989x_charger_info *info = dev_get_drvdata(dev);
    ktime_t now, add;
    unsigned int wakeup_ms = SC8989X_OTG_ALARM_TIMER_MS;
    int ret;

    if (!info) {
        pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
        return -EINVAL;
    }

    if (!info->otg_enable)
        return 0;

    cancel_delayed_work_sync(&info->wdt_work);
    cancel_delayed_work_sync(&info->cur_work);

    /* feed watchdog first before suspend */
    ret = sc8989x_update_bits(info, SC8989X_REG_7,
                REG07_TWD_MASK,REG07_TWD_MASK);
    if (ret)
        dev_warn(info->dev, "reset sc8989x failed before suspend\n");

    now = ktime_get_boottime();
    add = ktime_set(wakeup_ms / MSEC_PER_SEC,
            (wakeup_ms % MSEC_PER_SEC) * NSEC_PER_MSEC);
    alarm_start(&info->otg_timer, ktime_add(now, add));

    return 0;
}

static int sc8989x_charger_resume(struct device *dev)
{
	struct sc8989x_charger_info *info = dev_get_drvdata(dev);
    int ret;

    if (!info) {
        pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
        return -EINVAL;
    }

    if (!info->otg_enable)
        return 0;

    alarm_cancel(&info->otg_timer);

    /* feed watchdog first after resume */
    ret = sc8989x_update_bits(info, SC8989X_REG_7,
                REG07_TWD_MASK, REG07_TWD_MASK);
    if (ret)
        dev_warn(info->dev, "reset sc8989x failed after resume\n");

    schedule_delayed_work(&info->wdt_work, HZ * 15);
    schedule_delayed_work(&info->cur_work, 0);

    return 0;
}
#endif

static const struct dev_pm_ops sc8989x_charger_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sc8989x_charger_suspend,
				sc8989x_charger_resume)
};

static const struct i2c_device_id sc8989x_i2c_id[] = {
	{"sc8989x_chg", 0},
	{"sc8989x_slave_chg", 0},
	{}
};

static const struct of_device_id sc8989x_charger_of_match[] = {
	{ .compatible = "sc,sc8989x_chg", },
	{ .compatible = "sc,sc8989x_slave_chg", },
	{ }
};

MODULE_DEVICE_TABLE(of, sc8989x_charger_of_match);

static struct i2c_driver sc8989x_master_charger_driver = {
	.driver = {
		.name = "sc8989x_chg",
		.of_match_table = sc8989x_charger_of_match,
		.pm = &sc8989x_charger_pm_ops,
	},
	.probe = sc8989x_charger_probe,
	.shutdown = sc8989x_charger_shutdown,
	.remove = sc8989x_charger_remove,
	.id_table = sc8989x_i2c_id,
};

module_i2c_driver(sc8989x_master_charger_driver);
MODULE_DESCRIPTION("SC8989X Charger Driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(SC8989X_DRV_VERSION);
MODULE_AUTHOR("South Chip <boyu-wen@southchip.com>");
