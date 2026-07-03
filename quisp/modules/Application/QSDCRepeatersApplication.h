#ifndef MODULES_QSDCREPEATERSAPPLICATION_H_
#define MODULES_QSDCREPEATERSAPPLICATION_H_

#include "IApplication.h"
#include "modules/Logger/LoggerBase.h"
#include "utils/ComponentProvider.h"

#include "PhotonicQubit_m.h"
#include "backends/interfaces/IQubit.h"
#include "messages/classical_messages.h"
#include "messages/qsdc_messages_m.h"

#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace omnetpp {
class cMessage;
class cModule;
}  // namespace omnetpp

namespace quisp::modules {

struct LocalBellPair {
  int qnic_index;
  int qi_1;
  int qi_2;
  quisp::modules::StationaryQubit* qubit_1;
  quisp::modules::StationaryQubit* qubit_2;
};

enum class BellState { PhiPlus, PhiMinus, PsiPlus, PsiMinus };

class StationaryQubit;

class QSDCRepeatersApplication : public IApplication, public Logger::LoggerBase {
 public:
  QSDCRepeatersApplication() : provider(utils::ComponentProvider{this}) {}
  ~QSDCRepeatersApplication() override {}

 protected:
  int expected_bsms_count = 0;

  struct PauliTracker {
    bool apply_x = false;
    bool apply_z = false;
  };
  bool is_source = false;
  bool is_target = false;
  bool is_repeater = false;
  bool is_server = false;

  // Protocol parameters
  int total_qubits_to_send = 0;
  int current_qubit_index = 0;

  // FSM data
  bool source_ready = false;
  bool target_ready = false;
  bool source_received_current = false;
  bool target_received_current = false;

  // Async Memory and Purification Tracking
  std::map<int, quisp::backends::IQubit*> received_qubits;
  std::map<int, quisp::modules::StationaryQubit*> local_stored_qubits; // TODO: check difference
  std::map<int, int> bsm_arrival_counts;
  std::vector<int> ready_qubits;
  std::map<int, int> my_local_measurements;  // NEW: Safe map for your Z-basis results
  std::map<int, std::vector<quisp::modules::StationaryQubit*>> server_emitted_qubits;
  std::map<int, std::vector<quisp::modules::StationaryQubit*>> repeater_emitted_qubits;
  std::map<int, int> buffered_source_bsms;
  std::map<int, PauliTracker> cumulative_corrections;

  utils::ComponentProvider provider;
  int self_address = -1;
  bool is_initiator = false;

  bool waiting_for_sample_block = false;
  bool waiting_for_bell_block = false;

  int current_sample_block_sent = 0;
  int current_bell_block_sent = 0;

  struct PendingEntCheck {
    bool awaiting = true;
  };
  std::unordered_map<int, PendingEntCheck> pending_checks;

  bool source_continue_ready = false;
  bool target_continue_ready = false;
  bool server_is_rolling_back = false;  // Prevents duplicate rollbacks

  std::string secret_message;
  std::vector<int> bit_stream;  // The message converted to 0s and 1s
  std::vector<int> decoded_bit_stream;  // target's incoming bits
  int required_purified_pairs;  // Calculated based on message length

  std::vector<int> stored_purified_qubit_seqs;

  std::map<int, int> source_bsm_results;

  omnetpp::cMessage* qubit_reception_timeout_msg = nullptr;

  double channel_loss_rate = 0.0;
  double measurement_error_rate = 0.0;
  double gate_error_rate = 0.0;

  int source_address = -1;
  int target_address = -1;
  int server_address = -1;

  void applyDepolarizingNoise(quisp::backends::IQubit* qubit);

  omnetpp::cMessage* timeout_msg = nullptr;
  double timeout_interval = 0.0;

  void setMessage();
  void sendMessageSetup();
  void encodeAndPerformQSDC();
  void decodeQSDC();
  bool comm_end_received = false;

  void checkAndTriggerDecoding();

  // Aux functions
  void repeatQubit(int dst, quisp::modules::StationaryQubit* entangled_qubit);
  void measureBellStateAndSend(quisp::backends::IQubit* incoming_qubit, quisp::modules::StationaryQubit* local_qubit, int dst_addr, int seq_num);
  std::vector<LocalBellPair> generateEntangledPairs(int n, const char* qnic_type, int qnic_index, BellState state);
  omnetpp::cModule* getQNIC(const char* qnic_type, int qnic_index);
  void handleIncomingPhotonAtRepeater(quisp::messages::PhotonicQubit* photon);
  void handleIncomingPhotonAtEndNode(quisp::messages::PhotonicQubit* photon);
  int eigenToInt(quisp::backends::abstract::EigenvalueResult r);
  void sendNextQubitPair();
  void sendClassicalMessage(int dest_addr, const char* msg_type, const char* msg_name, int seq_num = -1, int meas_res = -1);
  void attemptPurification();
  void handleBSMResult(int seq_num, int bsm_outcome);
  void processReceptionTimeout(omnetpp::cMessage* msg);
  void cleanupRepeaterMemory();
  void cleanupLocalMemory(int seq_num);

  void processMessageSetup(quisp::messages::QSDCSynAck* pkt);
  void processQSDCPrepare(omnetpp::cMessage* msg);
  void processCommStart(omnetpp::cMessage* msg);

  void processCommReady(quisp::messages::QSDCSynAck* pkt);
  void processCommSync(quisp::messages::QSDCSynAck* pkt);
  void processCommAck(quisp::messages::QSDCSynAck* pkt);
  void processCommEnd(quisp::messages::QSDCSynAck* pkt);

  void processQubitSync(quisp::messages::QSDCSynAck* pkt);
  void processQubitAck(quisp::messages::QSDCSynAck* pkt);
  void processPurifyResult(quisp::messages::QSDCSynAck* pkt);

  void processQubitError(quisp::messages::QSDCSynAck* pkt);
  void processQubitDiscard(quisp::messages::QSDCSynAck* pkt);
  void processQubitContinue(quisp::messages::QSDCSynAck* pkt);

  // OMNeT specifics
  void initialize() override;
  void handleMessage(omnetpp::cMessage* msg) override;

  // Initialization:
  void protocolInit();
  void entCheckStartup(unsigned long ruleset_id);
};

}  // namespace quisp::modules

#endif
