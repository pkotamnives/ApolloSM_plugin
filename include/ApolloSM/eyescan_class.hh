#ifndef __EYESCANCLASS_HH__
#define __EYESCANCLASS_HH__
#include <ApolloSM/ApolloSM.hh>
//#include <BUException/ExceptionBase.hh>
#include <BUTool/ToolException.hh>
#include <IPBusIO/IPBusIO.hh>
//#include <ApolloSM/eyescan.hh>
#include <ApolloSM/ApolloSM_Exceptions.hh> // EYESCAN_ERROR
#include <vector>
#include <stdlib.h>
//#include <math.h> // pow
#include <map>
#include <syslog.h>
#include <time.h>

// Correct eye scan attribute values
#define ES_EYE_SCAN_EN 0x1
#define ES_ERRDET_EN 0x1
#define PMA_CFG 0x000 // Actually 10 0s: 10b0000000000
#define PMA_RSV2 0x1
#define ES_QUALIFIER 0x0000
#define ES_QUAL_MASK 0xFFFF

#define RX_DATA_WIDTH_GTX 0x4 // zynq
#define RX_INT_DATAWIDTH_GTX 0x1 // We use 32 bit

#define RX_DATA_WIDTH_GTH 0x4 // kintex
#define RX_INT_DATAWIDTH_GTH 0x0 //16 bit

#define RX_DATA_WIDTH_GTY 0x6 // virtex
#define RX_INT_DATAWIDTH_GTY 0x1 //32 bit


class eyescan
{
public:
	typedef enum { UNINIT, BUSY, WAITING_PRESCALE, DONE=0  } ES_state_t;

	// All necessary information to plot an eyescan
struct eyescanCoords {
  double voltage;
  double phase;
  double BER;
  uint32_t sample0;
  uint32_t error0;
  uint32_t sample1;
  uint32_t error1;
  uint8_t voltageReg;
  uint16_t phaseReg;
};

struct Coords {
  double voltage;
  double phase;
};
// struct pixel_out{ //single eyescan output
// 	double BER;
// 	uint32_t sample0;
// 	uint32_t error0;
// 	uint32_t sample1;
// 	uint32_t error1;
// };

private:
	ES_state_t es_state;
	std::vector<eyescanCoords> scan_output;
	std::vector<double> volt_vect;
	std::vector<double> phase_vect;
	int Max_prescale;
	float volt;
	float phase;



public:
	eyescan(std::string basenode, std::string lpmNode, int nBinsX, int nBinsY, int max_prescale);
	~eyescan();

	ES_state_t check();
	void update();
	std::vector<eyescanCoords> const & dataout();

private:
	eyescan();
	eyescanCoords scan_pixel(float phase, float volt, int prescale);
	//void ApolloSM::SetEyeScanPhase(std::string baseNode, /*uint16_t*/ int horzOffset, uint32_t sign);
	//void ApolloSM::SetEyeScanVoltage(std::string baseNode, uint8_t vertOffset, uint32_t sign);
	

};
	
// Does not need to be an ApolloSM function, only assertNode and confirmNode (below) will use this
void throwException(std::string message) {
  BUException::EYESCAN_ERROR e;
  e.Append(message);
  throw e;
}

// assert to the node the correct value. Must be an ApolloSM function to use RegWriteRegister and RegReadRegister
void ApolloSM::assertNode(std::string node, uint32_t correctVal) {
  RegWriteRegister(node, correctVal);
  // Might be able to just put confirmNode here
  if(correctVal != RegReadRegister(node)) {
    throwException("Unable to set " + node + " correctly to: " + std::to_string(correctVal));
  }
}

// confirm that the node value is correct. Must be an ApolloSM function to use RegReadRegister 
void ApolloSM::confirmNode(std::string node, uint32_t correctVal) {
  if(correctVal != RegReadRegister(node)) {
    throwException(node + " is not set correctly to: " + std::to_string(correctVal));
  }
}


#endif
