#include "QSDCRepeatersApplication.h"

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>

#include <omnetpp.h>

#include "messages/classical_messages.h"
#include "modules/QNIC/StationaryQubit/StationaryQubit.h"


// Tim's code to logging
namespace {
#define QLOG(expr)                         \
    do {                                   \
        std::ostringstream _qs;            \
        _qs << expr;                       \
        EV_INFO << _qs.str() << "\n";      \
        /* Keep file logging too if desired */ \
        std::ofstream logfile("qsdc_app.log", std::ios::app); \
        if (logfile.is_open()) {           \
            logfile << std::fixed << std::setprecision(9) \
                    << "[" << omnetpp::simTime() << "] " \
                    << _qs.str() << "\n";  \
            logfile.flush();               \
        }                                  \
    } while (0)
}



using namespace omnetpp;
using namespace quisp::messages;

namespace quisp::modules {

Define_Module(QSDCRepeatersApplication);

// Custom message values to define each node's behaviors in the protocol

static const char* SELF_GENERATE_PAIRS = "SELF_GENERATE_PAIRS";
static const char* SELF_WAIT_FOR_PAIRS = "WAIT_FOR_PAIRS";





void QSDCRepeatersApplication::initialize() {
    initializeLogger(provider);

        // Only keep this module for EndNodes (same logic as Application.cc)
        // Basically, only end nodes should run this.

        // I decided to use this module in repeater modules in the first instance, so this part might be useless

    // if (!gate("toRouter")->isConnected() || !gate("fromRouter")->isConnected()) {
    //   auto* msg = new DeleteThisModule("DeleteThisModule");
    //   scheduleAt(simTime(), msg);
    //   return;
    // }

    my_address = provider.getNodeAddr();

    auto* qnode = provider.getQNode();
    if (!qnode) {
        QLOG("[QSDC] No QNode found in initialize()");
        return;
    }

    // basic parameters to assign the roles of each node during communication
    // 4 roles: Alice and Bob (who want to communicate through QSDC)
    //          Server (who generates the entangled pairs on Psi-)
    //          Repeaters (who pass the messages with entanglement swapping)

    is_alice    = par("is_alice").boolValue();
    is_bob      = par("is_bob").boolValue();
    is_repeater = par("is_repeater").boolValue();
    is_server   = par("is_server").boolValue();
    is_test     = par("is_test").boolValue();
    
    if (is_test) {
        scheduleAt(simTime(), new cMessage(SELF_GENERATE_PAIRS));
    }
}


/* generateEntangledPairs 
*
*   generates in a QNIC the desired bell state to be sent or operated with during the protocol
*
*/
std::vector<LocalBellPair> QSDCRepeatersApplication::generateEntangledPairs(int n, const char* qnic_type, int qnic_index, BellState state) {
    std::vector<LocalBellPair> generated_pairs;
    
    if (n <= 0) return generated_pairs;

    auto* qnic = getQNIC(qnic_type, qnic_index);
    if (!qnic) {
        QLOG("[QSDC PairGen] ERROR: Target QNIC " << qnic_type << "[" << qnic_index << "] not found.");
        return generated_pairs;
    }

    const int num_buf = qnic->par("num_buffer").intValue();
    std::vector<int> free_indices;

    for (int i = 0; i < num_buf; i++) {
        auto* sq_mod = qnic->getSubmodule("statQubit", i);
        if (!sq_mod) continue;

        auto* sq = check_and_cast<quisp::modules::StationaryQubit*>(sq_mod);
        
        if (!sq->isBusy() && !sq->isLocked()) {
            free_indices.push_back(i); 
        }
        
        if (free_indices.size() == 2 * n) {
            break; // We have identified enough memory slots
        }
    }

    // atomic allocation check
    if (free_indices.size() < 2 * n) {
        QLOG("[QSDC PairGen] Insufficient memory. Requested " << n << " pairs (" << 2*n 
             << " qubits), but only " << free_indices.size() << " available in QNIC.");
        return generated_pairs; // caller must handle resource exhaustion
    }

    // lock hardware and apply quantum logic gates
    for (int i = 0; i < n; i++) {
        int qi_1 = free_indices[2 * i];
        int qi_2 = free_indices[2 * i + 1];

        auto* qubit_1 = check_and_cast<quisp::modules::StationaryQubit*>(qnic->getSubmodule("statQubit", qi_1));
        auto* qubit_2 = check_and_cast<quisp::modules::StationaryQubit*>(qnic->getSubmodule("statQubit", qi_2));

        // lock the qubits from the hardware layer upwards
        qubit_1->setBusy();
        qubit_2->setBusy();

        // maximum correlation base generation: |00> + |11> (Phi+)
        qubit_1->gateHadamard();
        qubit_1->gateCNOT(qubit_2);

        if (state == BellState::PhiPlus) {  
            // |00> + |11> (Base state, no operations needed)
        } else if (state == BellState::PhiMinus) { 
            // |00> - |11>
            qubit_1->gateZ(); 
        } else if (state == BellState::PsiPlus) {  
            // |01> + |10>
            qubit_2->gateX(); 
        } else if (state == BellState::PsiMinus) { 
            // |01> - |10>
            qubit_1->gateZ();
            qubit_2->gateX();
        }

        // Pack the physical references into the return struct
        generated_pairs.push_back({qnic_index, qi_1, qi_2, qubit_1, qubit_2});

        QLOG("[QSDC PairGen] Generated local Bell pair across qi=" << qi_1 << " and qi=" << qi_2 
             << " mapped to state enum: " << static_cast<int>(state));
    }

    return generated_pairs;
}


/*  QNIC = Quantum Network Interface Card :
*
*   Alice and Bob both will use QNIC as a Receiver (qnic_r)
*   Server will also use the QNIC as a Emitter (qnic)
*   Repeater will use both
*
*/  
omnetpp::cModule* QSDCRepeatersApplication::getQNIC(const char* qnic_type, int qnic_index) {
    auto* qnode = provider.getQNode();
    if (!qnode) return nullptr;

    // qnic_type will be "qnic", "qnic_r", or "qnic_rp"
    return qnode->getSubmodule(qnic_type, qnic_index);

    if (auto* m = qnode->getSubmodule("qnic_rp", 0)) return m;
    if (auto* m = qnode->getSubmodule("qnic_r", 0)) return m;
    if (auto* m = qnode->getSubmodule("qnic", 0)) return m;

    return nullptr;
}


void QSDCRepeatersApplication::handleMessage(cMessage *msg) {

  if (msg->isSelfMessage()) {
    if (strcmp(msg->getName(), SELF_GENERATE_PAIRS) == 0) {
        //int number_of_pairs = par("number_of_pairs").intValue();
        int number_of_pairs = 5;
        QLOG("[SERVER] Generating " << number_of_pairs << " Bell pairs for left and right links.");
        
        auto left_pairs = generateEntangledPairs(number_of_pairs, "qnic", 0, BellState::PhiPlus);
        auto right_pairs = generateEntangledPairs(number_of_pairs, "qnic", 1, BellState::PhiPlus);

        if (left_pairs.empty() || right_pairs.empty()) {
            QLOG("[SERVER] FATAL: Insufficient quantum memory to generate initial pairs. Aborting.");
            delete msg;
            return;
        }

        delete msg;
        return;
    }
  }

}



}  // namespace quisp::modules

