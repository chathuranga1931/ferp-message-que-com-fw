/*==============================================================================

    INCLUDES

==============================================================================*/
#include <SmingCore.h>

/*==============================================================================

    DEFINES

==============================================================================*/
#define SYS_SETTINGS_FILE ".syssettings.conf" // leading point for security reasons :)

#define SystemHostname "360global_pump"

#define HOST "http-ingress-alw5epn3aq-el.a.run.app"
#define PATH "/api/v1/device/data"
#define AUTHORIZATION "BasicZWUwOWQ2ZWMtOGY4OS00N2M2LTg4OWItNWRkMGYyMmUzNDMyOmEwZTFmNjFhLTM4NGEtMTFlZC1hMjYxLTAyNDJhYzEyMDAwMg=="
#define DEVICE_ID "ee09d6ec-8f89-47c6-889b-5dd0f22e3432"
#define PUMP_ID_1 "ee09d6ec-8f89-47c6-889b-5dd0f22e3433"
#define PUMP_ID_2 "ee09d6ec-8f89-47c6-889b-5dd0f22e3434"


/*==============================================================================

    TYPES

==============================================================================*/
typedef enum
{
    FUELke_Petrol_91 = 0,
    FUELke_Petrol_92 = 1,
    FUELke_Petrol_95 = 2,
    FUELke_Petrol_98 = 3,
    FUELke_Kerosen = 4,
    FUELke_Diesel = 5
} FUELteType;


typedef union
{
    uint32_t lw;
    uint8_t ab[4];
} wm_uHash_t;

class SysSettingsStorage
{
public:
    // signed char RTCtrim;
    bool STAmode;
    String sHexString; // remember generated hash as a String
    String HostName;
    String STAssid;
    String STApass;

    String Server;
    int16_t Port;
    String ID_Module;
    String ID_Nozzel_1;
    FUELteType FuelType_Nozzel_1;
    String ID_Nozzel_2;
    FUELteType FuelType_Nozzel_2;

    void init(void);
    void load(void);
    void save(void);
    void loadDeflt(void);
    bool exist(void);

private:
};

/*==============================================================================

    STATIC VARIABLES DECLARATIONS

==============================================================================*/

/*==============================================================================

    GLOBAL VARIABLE DECLARATIONS

==============================================================================*/
extern SysSettingsStorage SysSettings;
extern HardwareSerial Serial2;

/*==============================================================================

    STATIC FUNCTION DECLARATIONS

==============================================================================*/

/*==============================================================================

    FUNCTION DECLARATIONS

==============================================================================*/


