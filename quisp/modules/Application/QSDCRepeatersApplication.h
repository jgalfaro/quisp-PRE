#ifndef MODULES_QSDCREPEATERSAPPLICATION_H_
#define MODULES_QSDCREPEATERSAPPLICATION_H_

#include "IApplication.h"
#include "modules/Logger/LoggerBase.h"
#include "utils/ComponentProvider.h"

#include "PhotonicQubit_m.h"
#include "backends/interfaces/IQubit.h"

#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>


namespace omnetpp {
  class cMessage;
  class cModule;
}

namespace quisp::modules {

struct LocalBellPair {
    int qnic_index;
    int qi_1; // Index of the first half
    int qi_2; // Index of the second half
    quisp::modules::StationaryQubit* qubit_1;
    quisp::modules::StationaryQubit* qubit_2;
};

enum class BellState {
    PhiPlus,   // |00> + |11>
    PhiMinus,  // |00> - |11>
    PsiPlus,   // |01> + |10>
    PsiMinus   // |01> - |10>
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
  
    bool is_alice       = false;
    bool is_bob         = false;
    bool is_repeater    = false;
    bool is_server      = false;
    bool is_test        = false;

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

    struct PendingBellCheck {
      char basis;
      int bit;
    };
    std::unordered_map<int, PendingBellCheck> pending_bell_checks;

    // Misc Settings
    std::vector<int> current_sample_block_indices;
    std::vector<int> current_bell_block_indices;

    bool expect_anti_correlation = false;
    
    omnetpp::simtime_t start_delay = 0;
    omnetpp::simtime_t poll_interval = 0;
    omnetpp::simtime_t sample_interval = 0;
    
    std::unordered_set<int> used_indices;
    
    // Aux functions
    void repeatQubit(int dst, quisp::modules::StationaryQubit* entangled_qubit);
    void measureBellStateAndSend(quisp::backends::IQubit* incoming_qubit, quisp::modules::StationaryQubit* local_qubit, int dst_addr, int seq_num);
    std::vector<LocalBellPair> generateEntangledPairs(int n, const char* qnic_type, int qnic_index, BellState state);
    omnetpp::cModule* getQNIC(const char* qnic_type, int qnic_index);
    void handleIncomingPhotonAtRepeater(quisp::messages::PhotonicQubit* photon);
    int eigenToInt(quisp::backends::abstract::EigenvalueResult r);
    void forwardFlyingQubit(quisp::messages::PhotonicQubit* photon);
    int determineEgressGate(int dest_addr);
    // OMNeT specifics
    void initialize() override;
    void handleMessage(omnetpp::cMessage* msg) override;

    // Initialization:
    void protocolInit();
    void entCheckStartup(unsigned long ruleset_id);

    // Phase 1: Entanglement verification
    void startEntanglementCheck(int basis);
    void doNextEntanglementCheck(int basis);
    void sendEntanglementCheckRequest(int qi, char basis);

    // Phase 2: Quantum channel verification
    void doNextChannelCheck();
    void sendSamplePhoton(int qi, quisp::modules::StationaryQubit* qubit);

    // Phase 3: Superdense coding transmission
    void startDenseTransmission();
    void applyDenseEncoding(quisp::modules::StationaryQubit* qubit, const std::string& bits);
    void sendDensePhoton(int qi, quisp::modules::StationaryQubit* encoded_qubit);
    std::string decodeDensePair(
        quisp::modules::StationaryQubit* local_qubit,
        backends::IQubit* remote_qubit);

    // Utility:
    int countReadyPairsAndCollect(std::vector<int>& out_indices);
    void resetBlockState();
    int measureLocalInBasis(quisp::modules::StationaryQubit* qubit, char basis);
};

}  // namespace quisp::modules

#endif
