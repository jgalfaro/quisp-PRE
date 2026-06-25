#ifndef MODULES_QSDCREPEATERSAPPLICATION_H_
#define MODULES_QSDCREPEATERSAPPLICATION_H_

#include "IApplication.h"
#include "modules/Logger/LoggerBase.h"
#include "utils/ComponentProvider.h"

#include "PhotonicQubit_m.h"
#include "backends/interfaces/IQubit.h"
#include "messages/qsdc_messages_m.h"
#include "messages/classical_messages.h"

#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include <map>


namespace omnetpp {
  class cMessage;
  class cModule;
}

namespace quisp::modules {

struct LocalBellPair {
    int qnic_index;
    int qi_1; 
    int qi_2; 
    quisp::modules::StationaryQubit* qubit_1;
    quisp::modules::StationaryQubit* qubit_2;
};

enum class BellState {
    PhiPlus,   
    PhiMinus,  
    PsiPlus,   
    PsiMinus   
};

class StationaryQubit;

class QSDCRepeatersApplication : public IApplication, public Logger::LoggerBase {
  public:
  QSDCRepeatersApplication() : provider(utils::ComponentProvider{this}) {}
  ~QSDCRepeatersApplication() override {}
  
  protected:
  
  // Functions:
  // Protocol:
  /*
  * Test network connections
  * Server generates X entangled pairs in the phase psi- (idk if quisp makes phi+ naturally)
  * Server sends the bits through the network with repeaters etc
  *   hoping that quisp do it automatically
  *
  *
  *
  *
  *
  *
  *
  *
  *
  *
  *
  *
  *
  *
  */
  
    int expected_bsms_count = 0;

    // Add a tracker for cumulative Pauli corrections
    struct PauliTracker {
        bool apply_x = false;
        bool apply_z = false;
    };
    std::map<int, std::vector<quisp::modules::StationaryQubit*>> server_emitted_qubits;
    std::map<int, int> buffered_alice_bsms;
    // Map sequence number to its cumulative Pauli corrections
    std::map<int, PauliTracker> cumulative_corrections;
    bool is_alice       = false;
    bool is_bob         = false;
    bool is_repeater    = false;
    bool is_server      = false;
    bool is_test        = false;

    // Protocol parameters
    int total_qubits_to_send = 0;
    int current_qubit_index = 0;
    
    int server_address = 2; 
    int alice_address = 0;
    int bob_address = 4; // Assuming bob is 4 in your ini, update if needed.

    // FSM data
    bool alice_ready = false;
    bool bob_ready = false;
    bool alice_received_current = false;
    bool bob_received_current = false;

    // Async Memory and Purification Tracking
    std::map<int, quisp::backends::IQubit*> received_qubits;
    std::map<int, int> bsm_arrival_counts;       
    std::vector<int> ready_qubits;               
    std::map<int, int> my_local_measurements; // NEW: Safe map for your Z-basis results

    utils::ComponentProvider provider;
    int my_address = -1;
    bool is_initiator = false;


    // Eve attack settings
    bool eve_enabled = false;
    double eve_intercept_probability = 0.0;
    unsigned long active_ruleset_id = 0;
    bool sampling_started = false;
    bool protocol_started = false;
    bool bell_check_started = false;

    // Phase 1: QSDC Entanglement Check
    int bell_sample_target = 0;
    int bell_samples_done = 0;
    int bell_errors = 0;
    int bell_block_size = 8;

    bool waiting_for_sample_block = false;
    bool waiting_for_bell_block = false;

    int current_sample_block_sent = 0;
    int current_bell_block_sent = 0;

    struct PendingEntCheck {
      bool awaiting = true;
    };
    std::unordered_map<int, PendingEntCheck> pending_checks;

    // Phase 2: Quantum Channel Verification
    int burn_count = 0;
    int burn_current = 0;
    int min_pairs_to_start = 0;
    int sample_target = 0;
    int samples_done = 0;
    int errors = 0;
    int sample_block_size = 8;

    bool alice_continue_ready = false;
    bool bob_continue_ready = false;
    bool server_is_rolling_back = false; // Prevents duplicate rollbacks

    std::string secret_message;
    std::vector<int> bit_stream;         // The message converted to 0s and 1s
    std::vector<int> decoded_bit_stream; // Bob's incoming bits
    int required_purified_pairs;         // Calculated based on message length
    
    // Storage for qubits that have passed purification
    std::vector<int> stored_purified_qubit_seqs; 
    
    // QSDC Decoding Buffer at Bob
    std::map<int, int> alice_bsm_results; 

    // New internal methods
    void setMessage();
    void sendMessageSetup();
    void encodeAndPerformQSDC();
    void decodeQSDC();
    bool comm_end_received = false;

    // Add under New internal methods
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
    void sendClassicalMessage(int dest_addr, const char* msg_type, const char* msg_name, int seq_num  = -1, int meas_res = -1);
    void attemptPurification();
    void handleBSMResult(int seq_num, int bsm_outcome);

    // New Auxiliary Dispatch Handlers
    void processMessageSetup(quisp::messages::QSDCSynAck* pkt);
    void processQSDCPrepare(omnetpp::cMessage* msg);
    void processCommStart(omnetpp::cMessage* msg);
    
    // FSM State Handlers
    void processCommReady(quisp::messages::QSDCSynAck* pkt);
    void processCommSync(quisp::messages::QSDCSynAck* pkt);
    void processCommAck(quisp::messages::QSDCSynAck* pkt);
    void processCommEnd(quisp::messages::QSDCSynAck* pkt);
    
    // Qubit distribution and purification FSM Handlers
    void processQubitSync(quisp::messages::QSDCSynAck* pkt);
    void processQubitAck(quisp::messages::QSDCSynAck* pkt);
    void processPurifyResult(quisp::messages::QSDCSynAck* pkt);
    
    // ARQ (Automatic Repeat reQuest) Handlers
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
