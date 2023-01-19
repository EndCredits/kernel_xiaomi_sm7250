/*
 * cp_qc30.h
 *
 */

#ifndef SRC_PDLIB_USB_PD_POLICY_MANAGER_H_
#define SRC_PDLIB_USB_PD_POLICY_MANAGER_H_
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/workqueue.h>

typedef enum  {
    CP_STATE_ENTRY,
    CP_STATE_DISCONNECT,
    CP_STATE_SW_ENTRY,
    CP_STATE_SW_ENTRY_2,
//    CP_STATE_SW_ENTRY_3,
    CP_STATE_SW_LOOP,
    CP_STATE_FLASH2_ENTRY,
    CP_STATE_FLASH2_ENTRY_1,
 //   CP_STATE_FLASH2_ENTRY_2,
    CP_STATE_FLASH2_ENTRY_3,
 //  CP_STATE_FLASH2_GET_PPS_STATUS,
    CP_STATE_FLASH2_TUNE,
    CP_STATE_FLASH2_DELAY,
    CP_STATE_STOP_CHARGE,
} pm_sm_state_t;

#define	BAT_OVP_FAULT_SHIFT			0
#define	BAT_OCP_FAULT_SHIFT			1
#define	BUS_OVP_FAULT_SHIFT			2
#define	BUS_OCP_FAULT_SHIFT			3
#define	BAT_THERM_FAULT_SHIFT			4
#define	BUS_THERM_FAULT_SHIFT			5
#define	DIE_THERM_FAULT_SHIFT			6

#define	BAT_OVP_FAULT_MASK		(1 << BAT_OVP_FAULT_SHIFT)
#define	BAT_OCP_FAULT_MASK		(1 << BAT_OCP_FAULT_SHIFT)
#define	BUS_OVP_FAULT_MASK		(1 << BUS_OVP_FAULT_SHIFT)
#define	BUS_OCP_FAULT_MASK		(1 << BUS_OCP_FAULT_SHIFT)
#define	BAT_THERM_FAULT_MASK		(1 << BAT_THERM_FAULT_SHIFT)
#define	BUS_THERM_FAULT_MASK		(1 << BUS_THERM_FAULT_SHIFT)
#define	DIE_THERM_FAULT_MASK		(1 << DIE_THERM_FAULT_SHIFT)

#define	BAT_OVP_ALARM_SHIFT			0
#define	BAT_OCP_ALARM_SHIFT			1
#define	BUS_OVP_ALARM_SHIFT			2
#define	BUS_OCP_ALARM_SHIFT			3
#define	BAT_THERM_ALARM_SHIFT			4
#define	BUS_THERM_ALARM_SHIFT			5
#define	DIE_THERM_ALARM_SHIFT			6
#define BAT_UCP_ALARM_SHIFT			7

#define	BAT_OVP_ALARM_MASK		(1 << BAT_OVP_ALARM_SHIFT)
#define	BAT_OCP_ALARM_MASK		(1 << BAT_OCP_ALARM_SHIFT)
#define	BUS_OVP_ALARM_MASK		(1 << BUS_OVP_ALARM_SHIFT)
#define	BUS_OCP_ALARM_MASK		(1 << BUS_OCP_ALARM_SHIFT)
#define	BAT_THERM_ALARM_MASK		(1 << BAT_THERM_ALARM_SHIFT)
#define	BUS_THERM_ALARM_MASK		(1 << BUS_THERM_ALARM_SHIFT)
#define	DIE_THERM_ALARM_MASK		(1 << DIE_THERM_ALARM_SHIFT)
#define	BAT_UCP_ALARM_MASK		(1 << BAT_UCP_ALARM_SHIFT)

#define VBAT_REG_STATUS_SHIFT			0
#define IBAT_REG_STATUS_SHIFT			1

#define VBAT_REG_STATUS_MASK		(1 << VBAT_REG_STATUS_SHIFT)
#define IBAT_REG_STATUS_MASK		(1 << VBAT_REG_STATUS_SHIFT)

enum hvdcp3_type {
	HVDCP3_NONE = 0,
	HVDCP3_CLASSA_18W,
	HVDCP3_CLASSB_27W,
};

#define HVDCP3_CLASS_B_BAT_CURRENT_MA			5600
#define HVDCP3_CLASS_B_BUS_CURRENT_MA			2700
#define HVDCP3_CLASS_A_BAT_CURRENT_MA			3800
#define HVDCP3_CLASS_A_BUS_CURRENT_MA			2200
#define MAX_THERMAL_LEVEL				13
/* jeita related */
#define JEITA_WARM_THR					450
#define JEITA_COOL_NOT_ALLOW_CP_THR			100
/*
 * add hysteresis for warm threshold to avoid flash
 * charge and normal charge switch frequently at
 * the warm threshold
 */
#define JEITA_HYSTERESIS			20

#define HIGH_CAPACITY_TRH			90

struct flash2_policy {
	int down_steps;
	uint16_t volt_hysteresis;
};

#if 0
struct bq2597x_cfg {
	uint16_t bat_ovp_th;
	uint16_t bat_ocp_th;
	uint16_t bus_ovp_th;
	uint16_t bus_ocp_th;


	uint16_t bat_ucp_alarm_th;

	uint16_t bat_ovp_alarm_th;
	uint16_t bat_ocp_alarm_th;
	uint16_t bus_ovp_alarm_th;
	uint16_t bus_ocp_alarm_th;

	uint16_t ac_ovp_th;
	uint16_t ts_bus_fault_th;
	uint16_t ts_bat_fault_th;
	uint16_t ts_die_fault_th;

	/* software protection threshold*/
	int32_t sw_bat_ovp_th;
	int32_t sw_bat_ocp_th;
	int32_t sw_bus_ovp_th;
	int32_t sw_bus_ocp_th;
	int32_t sw_bat_ucp_th;
	int32_t taper_current;
};
#endif

struct bq2597x {
	bool charge_enabled;

	bool batt_pres;
	bool vbus_pres;

	/* alarm/fault status */
	bool bat_ovp_fault;
	bool bat_ocp_fault;
	bool bus_ovp_fault;
	bool bus_ocp_fault;

	bool bat_ovp_alarm;
	bool bat_ocp_alarm;
	bool bus_ovp_alarm;
	bool bus_ocp_alarm;

	bool bat_ucp_alarm;

	bool bat_therm_alarm;
	bool bus_therm_alarm;
	bool die_therm_alarm;

	bool bat_therm_fault;
	bool bus_therm_fault;
	bool die_therm_fault;

	bool therm_shutdown_flag;
	bool therm_shutdown_stat;

	bool vbat_reg;
	bool ibat_reg;

	int  vout_volt;
	int  vbat_volt;
	int  vbus_volt;
	int  ibat_curr;
	int  ibus_curr;

	int  bat_temp;
	int  bus_temp;
	int  die_temp;
};

struct sw_charger {
	bool charge_enabled;
	bool charge_limited;
};

#define PM_STATE_LOG_MAX    32
typedef struct {
	bool        sw_is_charging;
	bool        flash2_is_charging;
	bool        sw_from_flash2;
	bool        sw_near_cv;
	bool		sw_fc2_init_fail;
	bool		bms_fastcharge_mode;

	uint16_t		ibus_lmt_curr;
	pm_sm_state_t     state; //state machine
	pm_sm_state_t state_log[PM_STATE_LOG_MAX];
	uint8_t     log_idx;

	int			usb_type;
	int			hvdcp3_type;
	int			usb_present;
	int			ibat_now;
	int			capacity;
	struct bq2597x			bq2597x;
	struct sw_charger			sw_chager;
	struct votable		*fcc_votable;

	struct power_supply *fc_psy;
	struct power_supply *sw_psy;
	struct power_supply *usb_psy;
	struct power_supply *bms_psy;
	/* jeita or thermal related */
	bool			jeita_triggered;
	bool			is_temp_out_fc2_range;

    struct delayed_work	qc3_pm_work;
} pm_t;


struct sys_config {
	uint16_t bat_volt_lp_lmt; /*bat volt loop limit*/
	uint16_t bat_curr_lp_lmt;
	uint16_t bus_volt_lp_lmt;
	uint16_t bus_curr_lp_lmt;
	int32_t fc2_taper_current;

	struct flash2_policy flash2_policy;

	uint16_t min_vbat_start_flash2;
	bool	cp_sec_enable;
};

#endif /* SRC_PDLIB_USB_PD_POLICY_MANAGER_H_ */
