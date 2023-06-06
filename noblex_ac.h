#include "esphome.h"
static const char *TAG = "noblex.climate";
uint8_t modo_actual_;
uint8_t modo_previo_;

uint8_t remote_state[4] = {0x80, 0x10, 0x00, 0x0A};	
uint8_t remote_state_2[4] = {0x00, 0x02, 0x00, 0x0F};	// COOL, 24C, FAN_AUTO
uint8_t aux;
uint8_t powered_on;
// ------ Detalle de la trama de datos ------
// Cabecera 
// 0.0 Mode: Auto, Cool, Dry, Fan, Heat. *** Power Off restar 1.
// 0.1 Fan:  Auto, Low, Medium, High. para activar Flap vertical sumar 2.
// 1.0 Temperature: NOBLEX_TEMP_MAP[],donde 16 es 0000 y 30 es 0111 (va de 0 a 14 pero con bits rotados)
// 1.1 Cero: 0
// 2.0 Cero: 0
// 2.1 Turbo: Al encender poner un 2. al apagar poner un 0.  
// 3.0 Cero: 0
// 3.1 A: A
// Separador -> 0-1-0-GAP (20000us)
// 4   Cero: 0
// 5   Dos:  2
// 6   Cero: 0
// 7   -> NOBLEX_TEMP_MAP_COOL[]. Si modo es OFF restar 1 
//	   -> NOBLEX_TEMP_MAP_HEAT[]. Si modo es OFF sumar 1    
// Pie
// --------------------------------------------
typedef enum IRNoblexMode {			//off:	on:
	IRNoblexModeAuto = 0x00,		//0		1 	invertidos 	D
    IRNoblexModeCool = 0x80,		//8 	9				6
	IRNoblexModeDry  = 0x40,		//4 	5				A
	IRNoblexModeFan  = 0xC0,		//C 	D				2
    IRNoblexModeHeat = 0x20,		//2 	3				C
 } IRNoblexMode;
 
typedef enum IRNoblexFan {			// Estos van Enmascarados con mode
	IRNoblexFanAuto   = 0x00,		//0 
    IRNoblexFanLow    = 0x08,		//8
    IRNoblexFanMedium = 0x04,		//4
    IRNoblexFanHigh   = 0x0C,		//C
 } IRNoblexFan;

const uint8_t NOBLEX_TEMP_MIN = 16;  // Celsius
const uint8_t NOBLEX_TEMP_MAX = 30;  // Celsius
const uint8_t NOBLEX_TEMP_RANGE = NOBLEX_TEMP_MAX - NOBLEX_TEMP_MIN + 1;	//15 valores
const uint8_t NOBLEX_TEMP_MAP[NOBLEX_TEMP_RANGE] = {
    0x00,  // 16C				
    0x80,  // 17C		
    0x40,  // 18C
    0xC0,  // 19C
    0x20,  // 20C
    0xA0,  // 21C
    0x60,  // 22C
    0xE0,  // 23C
    0x10,  // 24C
    0x90,  // 25C
	0x50,  // 26C
    0xD0,  // 27C  
    0x30,  // 28C
    0xB0,  // 29C
    0x70,  // 30C     
};
const uint8_t NOBLEX_MAP_COOL[NOBLEX_TEMP_RANGE] = {
    0x0E,  // 16C		E
    0x01,  // 17C		1
    0x09,  // 18C		9
    0x05,  // 19C		5
    0x0D,  // 20C		D
    0x03,  // 21C		3
    0x0B,  // 22C		B
    0x07,  // 23C		7
    0x0F,  // 24C		F
    0x00,  // 25C		0
	0x08,  // 26C		8
    0x04,  // 27C   	4
    0x0C,  // 28C		C
    0x02,  // 29C		2
    0x0A,  // 30C   	A  
};
const uint8_t NOBLEX_MAP_HEAT[NOBLEX_TEMP_RANGE] = {
    0x05,  // 16C		5
    0x0D,  // 17C		D
    0x03,  // 18C		3
    0x0B,  // 19C		B
    0x07,  // 20C		7
    0x0F,  // 21C		F
    0x00,  // 22C		0
	0x08,  // 23C		8
    0x04,  // 24C   	4
    0x0C,  // 25C		C
    0x02,  // 26C		2
    0x0A,  // 27C   	A  
	0x06,  // 28C		6
	0x0E,  // 29C		E
    0x01,  // 30C		1
};
const uint8_t NOBLEX_MAP_FAN[NOBLEX_TEMP_RANGE] = {
    0x09,  // 16C		9
    0x05,  // 17C		5
    0x0D,  // 18C		D
    0x03,  // 19C		3
    0x0B,  // 20C		B
    0x07,  // 21C		7
    0x0F,  // 22C		F
	0x00,  // 23C		0
    0x08,  // 24C   	8
    0x04,  // 25C		4
    0x0C,  // 26C		C
    0x02,  // 27C   	2 
	0x0A,  // 28C		A
	0x06,  // 29C		6
    0x0E,  // 30C		E
};
/* scaled values to get correct timing on ESP8266/ESP32: */
const uint16_t NOBLEX_HEADER_MARK =	 	9000;
const uint16_t NOBLEX_HEADER_SPACE =	4500;
const uint16_t NOBLEX_BIT_MARK =		660;		//650
const uint16_t NOBLEX_ONE_SPACE = 		1640;		//1660
const uint16_t NOBLEX_ZERO_SPACE = 		520;		//530
const uint32_t NOBLEX_GAP = 			20000;

//********************************************************
//******* Definicion de la Clase NoblexClimate ***********
//********************************************************
class NoblexClimate : public climate::Climate, public Component {
	
 public:
 
 void setup() override {
    if (this->sensor_) {
		this->sensor_->add_on_state_callback([this](float state) {
			this->current_temperature = state;
			// current temperature changed, publish state
			this->publish_state();
		});
		this->current_temperature = this->sensor_->state;
    }
	else
		this->current_temperature = NAN;

    // restore set points
    auto restore = this->restore_state_();
    if (restore.has_value()) {
		restore->apply(this);
    }
	else {					// *** restore from defaults ***
		this->mode = climate::CLIMATE_MODE_OFF;			// modo por defecto al restaurar		
		modo_actual_ = IRNoblexModeCool;
		this->target_temperature = 24;					// roundf(this->current_temperature);
		this->fan_mode = climate::CLIMATE_FAN_AUTO;
		this->swing_mode = climate::CLIMATE_SWING_OFF;
    }
	
    this->active_mode_ = this->mode;
  }
  
//********************************************************
// Asigno Tx remoto, Sensor de Temp y funcionalidad minima
 void set_transmitter(remote_transmitter::RemoteTransmitterComponent *transmitter) {
	this->transmitter_ = transmitter;
 }
 void set_sensor(sensor::Sensor *sensor) { this->sensor_ = sensor; }
 
 void set_supports_cool(bool supports_cool) { this->supports_cool_ = supports_cool; }
 void set_supports_heat(bool supports_heat) { this->supports_heat_ = supports_heat; } 

//********************************************************
// Override control to change settings of the climate device
 void control(const climate::ClimateCall &call) override {
    if (call.get_mode().has_value())
		this->mode = *call.get_mode();
    if (call.get_target_temperature().has_value())
		this->target_temperature = *call.get_target_temperature();
	if (call.get_fan_mode().has_value())
		this->fan_mode = *call.get_fan_mode();
	if (call.get_swing_mode().has_value())
		this->swing_mode = *call.get_swing_mode(); 	  
    this->transmit_state_();
    this->publish_state();

    this->active_mode_ = this->mode;			// ***Ver si uso esta variable
  }
//********************************************************
// Return the traits of this controller
 climate::ClimateTraits traits() override {
	auto traits = climate::ClimateTraits();
    traits.set_supports_current_temperature(this->sensor_ != nullptr);
	
	traits.set_supported_modes({climate::CLIMATE_MODE_OFF,
								//climate::CLIMATE_MODE_AUTO,
								//climate::CLIMATE_MODE_HEAT_COOL,
								});
    if (supports_cool_)
		traits.add_supported_mode(climate::CLIMATE_MODE_COOL);
	if (supports_heat_)
		traits.add_supported_mode(climate::CLIMATE_MODE_HEAT);
	if (supports_dry_)
		traits.add_supported_mode(climate::CLIMATE_MODE_DRY);
	if (supports_fan_only_)
		traits.add_supported_mode(climate::CLIMATE_MODE_FAN_ONLY);

    traits.set_supports_two_point_target_temperature(false);
	
	traits.set_supported_fan_modes({climate::CLIMATE_FAN_AUTO,
									climate::CLIMATE_FAN_LOW,
									climate::CLIMATE_FAN_MEDIUM,
									climate::CLIMATE_FAN_HIGH});

	traits.set_supported_swing_modes({climate::CLIMATE_SWING_OFF,
									climate::CLIMATE_SWING_VERTICAL});
    
	traits.set_visual_min_temperature(NOBLEX_TEMP_MIN);
    traits.set_visual_max_temperature(NOBLEX_TEMP_MAX);
    traits.set_visual_temperature_step(1);
    return traits;
 }

//********************************************************
// Transmit the state of this climate controller via IR
 void transmit_state_() {
	
	//***** Temperature *****
	auto temp = (uint8_t) roundf(clamp<float>(this->target_temperature, NOBLEX_TEMP_MIN, NOBLEX_TEMP_MAX));
	remote_state[1] = NOBLEX_TEMP_MAP[temp - NOBLEX_TEMP_MIN];
	
	if(this->mode == climate::CLIMATE_MODE_COOL) {
		remote_state_2[3] = NOBLEX_MAP_COOL[temp - NOBLEX_TEMP_MIN]; }
	if(this->mode == climate::CLIMATE_MODE_HEAT) {
		remote_state_2[3] = NOBLEX_MAP_HEAT[temp - NOBLEX_TEMP_MIN]; }
	if(this->mode == climate::CLIMATE_MODE_FAN_ONLY) {
		remote_state_2[3] = NOBLEX_MAP_FAN[temp - NOBLEX_TEMP_MIN]; }
	
	aux = remote_state_2[3]; 
	
	//***** Mode *****
    switch (this->mode) {
		case climate::CLIMATE_MODE_COOL:
			remote_state[0] = IRNoblexMode::IRNoblexModeCool;
			remote_state[0]|= 0x10;		// OR con bit de encendido
			remote_state[2] = 0x02;		// Al encender un 2
			modo_actual_ = IRNoblexMode::IRNoblexModeCool;    
			powered_on = 1;
			break;
		case climate::CLIMATE_MODE_HEAT:
			remote_state[0] = IRNoblexMode::IRNoblexModeHeat;
			remote_state[0]|= 0x10;
			remote_state[2] = 0x02;
			modo_actual_ = IRNoblexMode::IRNoblexModeHeat;		
			powered_on = 1;
			break;
		case climate::CLIMATE_MODE_FAN_ONLY:
			remote_state[0] = IRNoblexMode::IRNoblexModeFan;
			remote_state[0]|= 0x10;		// OR con bit de encendido
			remote_state[2] = 0x02;		// Al encender un 2
			modo_actual_ = IRNoblexMode::IRNoblexModeFan;
			powered_on = 1;
			break;
		case climate::CLIMATE_MODE_AUTO:
			remote_state[0] = IRNoblexMode::IRNoblexModeAuto;
			remote_state[0]|= 0x10;
			remote_state[1] = 0x90;		// NOBLEX_TEMP_MAP de 25C
			remote_state[2] = 0x02;
			remote_state_2[3] = 0x0F;	// FF
			modo_actual_ = IRNoblexMode::IRNoblexModeAuto;
			powered_on = 1;
			break;
		case climate::CLIMATE_MODE_OFF:
		default:
			if(powered_on) {
				if(modo_actual_ == IRNoblexModeCool) {
					if(temp >= 25) {aux += 0x01;}
					if(temp < 25) {aux -= 0x01;}
				}
				if(modo_actual_ == IRNoblexModeHeat) {
					if(temp >= 22) {aux += 0x01;}
					if(temp < 22) {aux -= 0x01;}
				}
				if(modo_actual_ == IRNoblexModeFan) {
					if(temp >= 23) {aux += 0x01;}
					if(temp < 23) {aux -= 0x01;}
				}
			}
			powered_on = 0;
			remote_state[0] = modo_actual_;			
			remote_state[2] = 0x00;			
			remote_state_2[3] = aux;			
			break;
    }
	//***** Fan Speed *****		disposicion: mode|fan -> enmascaro con fan
	// implementacion de modificacion de fan 
    // Ej. remote_state[0] |= IRNoblexFan::IRNoblexFanAuto;
	switch (this->fan_mode.value()) {		
		case climate::CLIMATE_FAN_LOW:
			remote_state[0] |= IRNoblexFan::IRNoblexFanLow;
			break;
		case climate::CLIMATE_FAN_MEDIUM:
			remote_state[0] |= IRNoblexFan::IRNoblexFanMedium;
			break;	  
		case climate::CLIMATE_FAN_HIGH:
			remote_state[0] |= IRNoblexFan::IRNoblexFanHigh;
			break;	 
		case climate::CLIMATE_FAN_AUTO:
		default:
			remote_state[0] |= IRNoblexFan::IRNoblexFanAuto;
			break;
	}
		
	//***** Swing *****	
	switch (this->swing_mode) {										
	case climate::CLIMATE_SWING_VERTICAL:
		remote_state[0] |= 0x02;			// set bit 2
		remote_state_2[0] = 0x80;				
		break;
	case climate::CLIMATE_SWING_OFF:
	default:
		remote_state[0] &= 0xFD;			// reset bit 2
		remote_state_2[0] = 0x00;				
		break;
	}
	
    ESP_LOGD(TAG, "Sending noblex code-> %02X%02X %02X%02X - %02X%02X %02X%02X",
	remote_state[0], remote_state[1], remote_state[2], remote_state[3],
	remote_state_2[0], remote_state_2[1], remote_state_2[2], remote_state_2[3]);

    auto transmit = this->transmitter_->transmit();
    auto data = transmit.get_data();

    data->set_carrier_frequency(38000);
	//----------------------------	
	// Header
	data->mark(NOBLEX_HEADER_MARK);
	data->space(NOBLEX_HEADER_SPACE);
	//----------------------------	
	// Data (sent from the MSB to the LSB)
	for (uint8_t i : remote_state)		
		for (int8_t j = 7; j >= 0; j--) {
			data->mark(NOBLEX_BIT_MARK);
			bool bit = i & (1 << j);
			data->space(bit ? NOBLEX_ONE_SPACE : NOBLEX_ZERO_SPACE);
		}
	//----------------------------		
	// Header intermedio
	data->mark(NOBLEX_BIT_MARK);
	data->space(NOBLEX_ZERO_SPACE);		// cero
	data->mark(NOBLEX_BIT_MARK);
	data->space(NOBLEX_ONE_SPACE);		// uno
	data->mark(NOBLEX_BIT_MARK);
	data->space(NOBLEX_ZERO_SPACE);		// cero
	data->mark(NOBLEX_BIT_MARK);
	data->space(NOBLEX_GAP);			// gap
	//----------------------------	
	for (uint8_t i : remote_state_2)		
		for (int8_t j = 7; j >= 0; j--) {
			data->mark(NOBLEX_BIT_MARK);
			bool bit = i & (1 << j);
			data->space(bit ? NOBLEX_ONE_SPACE : NOBLEX_ZERO_SPACE);
		}
	//----------------------------	
	// Footer
	data->mark(NOBLEX_BIT_MARK);
	//data->space(NOBLEX_GAP);
    
	transmit.perform();
  }
//********************************************************
// mas declaraciones
  ClimateMode active_mode_;

  bool supports_cool_{true};
  bool supports_heat_{true};
  bool supports_dry_{false};
  bool supports_fan_only_{true};
  
  remote_transmitter::RemoteTransmitterComponent *transmitter_;
  sensor::Sensor *sensor_{nullptr};
};