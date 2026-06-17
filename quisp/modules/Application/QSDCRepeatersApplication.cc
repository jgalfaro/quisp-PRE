#include "QSDCRepeatersApplication.h"

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>

#include <omnetpp.h>

#include "messages/classical_messages.h"
#include "messages/qsdc_messages_m.h"
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
static const char* SELF_WAIT_FOR_PAIRS = "SELF_WAIT_FOR_PAIRS";
static const char* SELF_RECEPTION_TIMEOUT = "SELF_RECEPTION_TIMEOUT";
static const char* SELF_SEND_PHOTON = "SELF_SEND_PHOTON";

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
        scheduleAt(simTime(), new cMessage(SELF_SEND_PHOTON ));
    }
}

void QSDCRepeatersApplication::protocolInit() {
    // override the standard connection manager for testing the physical layer swap
    if (is_initiator) {
        QLOG("[QSDC Alice] Bypassing Connection Manager for direct Server test.");
        // delay long enough for Server to generate, Repeater to swap, and Bob to apply corrections
        scheduleAt(simTime() + start_delay + par("sample_interval"), new cMessage(SELF_WAIT_FOR_PAIRS));
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
            QLOG("[QSDC PairGen] Generating local Bell pair across qi=" << qi_1 << " and qi=" << qi_2  << " mapped to state: Phi+");  
            // |00> + |11> (Base state, no operations needed)
        } else if (state == BellState::PhiMinus) { 
            QLOG("[QSDC PairGen] Generating local Bell pair across qi=" << qi_1 << " and qi=" << qi_2  << " mapped to state: Phi-");  
            // |00> - |11>
            qubit_1->gateZ(); 
        } else if (state == BellState::PsiPlus) {  
            QLOG("[QSDC PairGen] Generating local Bell pair across qi=" << qi_1 << " and qi=" << qi_2 << " mapped to state: Psi+");  
            // |01> + |10>
            qubit_2->gateX(); 
        } else if (state == BellState::PsiMinus) { // Actually used on the protocol
            QLOG("[QSDC PairGen] Generating local Bell pair across qi=" << qi_1 << " and qi=" << qi_2 << " mapped to state: Psi-");  
            // |01> - |10>
            qubit_1->gateZ();
            qubit_2->gateX();
        }

        // Pack the physical references into the return struct
        generated_pairs.push_back({qnic_index, qi_1, qi_2, qubit_1, qubit_2});

    }

    return generated_pairs;
}

/* measureBellStateAndSend
*   grabs 2 bits and applies CNOT and Hadammard gates to perform the ES
*   It will be used on the "Repeater" function
*
*/

void QSDCRepeatersApplication::measureBellStateAndSend(quisp::backends::IQubit* incoming_qubit, quisp::modules::StationaryQubit* local_qubit, int dst_addr, int seq_num) {
    if (!incoming_qubit || !local_qubit) {
        QLOG("[QSDC BSM] ERROR: Null qubit references passed to measureBellStateAndSend.");
        return;
    }

    // entanglement Swapping gates
    incoming_qubit->gateCNOT(local_qubit->getBackendQubitRef());
    incoming_qubit->gateH();

    int z0_result = eigenToInt(incoming_qubit->measureZ());
    int z1_result = eigenToInt(local_qubit->measureZ());


    int bsm_outcome = 0;
    if (z0_result == +1 && z1_result == +1)      {
        QLOG("[QSDC BSM] Entanglement Swapping successful. Outcome: Phi+");
        bsm_outcome = 0; 
    }      // Phi+
    else if (z0_result == -1 && z1_result == +1) {
        QLOG("[QSDC BSM] Entanglement Swapping successful. Outcome: Phi-");
        bsm_outcome = 1; 
    } // Phi-
    else if (z0_result == +1 && z1_result == -1) {
        QLOG("[QSDC BSM] Entanglement Swapping successful. Outcome: Psi+");
        bsm_outcome = 2; 
    } // Psi+
    else if (z0_result == -1 && z1_result == -1) {
        QLOG("[QSDC BSM] Entanglement Swapping successful. Outcome: Psi-");
        bsm_outcome = 3; 
    } // Psi-


    local_qubit->Unlock(); 

    // send classical Pauli-frame correction data
    if(dst_addr != -1) {
        QSDCBSMResult* bsm_packet = new QSDCBSMResult("BSM_Announcement");
        bsm_packet->setSrcAddr(my_address);
        bsm_packet->setDestAddr(dst_addr); // Fixed variable name here
        bsm_packet->setBsmOutcome(bsm_outcome);
        bsm_packet->setSequenceNum(seq_num);
        send(bsm_packet, "toRouter");
    }
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


void QSDCRepeatersApplication::handleIncomingPhotonAtRepeater(quisp::messages::PhotonicQubit* photon) {
    // step 1: create a new pair of entangled qubits on the psi- state
    auto new_pairs = generateEntangledPairs(1, "qnic", 1, BellState::PsiMinus);
    int seq_num = (int)photon->par("sequence_number").longValue();
    if (new_pairs.empty()) {
        QLOG("[REPEATER] FATAL: Insufficient quantum memory to generate ES pair. Dropping photon.");
        delete photon;
        return;
    }

    auto* local_half = new_pairs[0].qubit_1;  
    auto* remote_half = new_pairs[0].qubit_2; 

    // extract the incoming backend IQubit from the OMNeT++ message
    auto* incoming_qubit = const_cast<quisp::backends::IQubit*>(photon->getQubitRef());
    int bob_addr = par("bob_addr").intValue();

    // step 2: entanglement swap with the incoming bit 
    measureBellStateAndSend(incoming_qubit, local_half, bob_addr, seq_num);

    // free the local qubit memory so it can be reused in future cycles
    local_half->setFree(true);

    // step 3: send the remaining bit to the next repeater or module
    auto* next_photon = new quisp::messages::PhotonicQubit("FORWARDED_PHOTON");
    next_photon->setQubitRef(remote_half->getBackendQubitRef());
    
    // pass original metadata forward
    next_photon->addPar("src_addr") = my_address;
    next_photon->addPar("qubit_index") = new_pairs[0].qi_2;
    next_photon->addPar("sequence_number") = seq_num;

    send(next_photon, "toQuantum", 1);

    // clean up 
    delete photon;
}


// Utility mapper for eigenvalues
int QSDCRepeatersApplication::eigenToInt(quisp::backends::abstract::EigenvalueResult r) {
    return (r == quisp::backends::abstract::EigenvalueResult::PLUS_ONE) ? +1 : -1;
}


void QSDCRepeatersApplication::handleMessage(cMessage *msg) {


    // if (auto* photon = dynamic_cast<quisp::messages::PhotonicQubit*>(msg)) {
    //     if (is_repeater) {
    //         handleIncomingPhotonAtRepeater(photon);
    //     } 
    //     else if (is_alice) {
    //         // Alice intercepts the server's left photon and tests it
    //         int seq_num = (int)photon->par("sequence_number").longValue();
    //         auto* qubit = const_cast<quisp::backends::IQubit*>(photon->getQubitRef());
            
    //         // Measure in Z-basis
    //         int alice_res = eigenToInt(qubit->measureZ());
    //         QLOG("[QSDC Alice] Measured seq_num=" << seq_num << " in Z-basis. Result: " << alice_res);
            
    //         // Send result to Bob via classical channel
    //         QSDCBSMResult* bell_req = new QSDCBSMResult("BELL_REQ");
    //         bell_req->setSrcAddr(my_address);
    //         bell_req->setDestAddr(par("bob_addr").intValue());
    //         bell_req->setSequenceNum(seq_num);
    //         bell_req->setBsmOutcome(alice_res); // Hijacking this field for Alice's result
            
    //         send(bell_req, "toRouter");
    //         delete photon; // Cleanup physical photon wrapper
    //     }
    //     else if (is_bob) {
    //         // Bob buffers the forwarded photon from the repeater
    //         int seq_num = (int)photon->par("sequence_number").longValue();
    //         bob_buffered_photons[seq_num] = photon;
    //         evaluateBobTest(seq_num); // Attempt evaluation
    //     }
    //     else {
    //         delete photon;
    //     }
    //     return;
    // }

    // if (strcmp(msg->getName(), "BSM_Announcement") == 0) {
    //     auto* bsm_msg = check_and_cast<QSDCBSMResult*>(msg);
    //     int seq_num = bsm_msg->getSequenceNum();
    //     int bsm_outcome = bsm_msg->getBsmOutcome();
        
    //     if (is_bob) {
    //         bob_bsm_outcomes[seq_num] = bsm_outcome;
    //         evaluateBobTest(seq_num);
    //     }
    //     delete msg;
    //     return;
    // }

    // if (strcmp(msg->getName(), "BELL_REQ") == 0) {
    //     auto* req_msg = check_and_cast<QSDCBSMResult*>(msg);
    //     int seq_num = req_msg->getSequenceNum();
    //     int alice_res = req_msg->getBsmOutcome(); 
        
    //     if (is_bob) {
    //         bob_alice_results[seq_num] = alice_res;
    //         evaluateBobTest(seq_num);
    //     }
    //     delete msg;
    //     return;
    // }

    // if (strcmp(msg->getName(), SELF_GENERATE_PAIRS) == 0) {
    //     int number_of_pairs = par("number_of_bellpair").intValue(); 
    //     QLOG("[SERVER] Generating " << number_of_pairs << " Bell pairs for left and right links.");
        
    //     auto left_pairs = generateEntangledPairs(number_of_pairs, "qnic", 0, BellState::PsiMinus);
    //     auto right_pairs = generateEntangledPairs(number_of_pairs, "qnic", 1, BellState::PsiMinus);

    //     if (left_pairs.empty() || right_pairs.empty()) {
    //         QLOG("[QSDC SERVER] FATAL: Insufficient quantum memory. Aborting.");
    //         delete msg;
    //         return;
    //     }

    //     // Emit to Alice
    //     for (size_t i = 0; i < left_pairs.size(); ++i) {
    //         auto* photon_left = new quisp::messages::PhotonicQubit("SERVER_PHOTON_LEFT");
    //         photon_left->setQubitRef(left_pairs[i].qubit_1->getBackendQubitRef());
    //         photon_left->addPar("src_addr") = my_address;
    //         photon_left->addPar("sequence_number") = (int)i; 
    //         send(photon_left, "toQuantum", 0); 
    //     }

    //     // Emit to Repeater -> Bob
    //     for (size_t i = 0; i < right_pairs.size(); ++i) {
    //         auto* photon_right = new quisp::messages::PhotonicQubit("SERVER_PHOTON_RIGHT");
    //         photon_right->setQubitRef(right_pairs[i].qubit_2->getBackendQubitRef());
    //         photon_right->addPar("src_addr") = my_address;
    //         photon_right->addPar("sequence_number") = (int)i; 
    //         send(photon_right, "toQuantum", 1);
    //     }

    //     delete msg;
    //     return;
    // }

    my_address = provider.getNodeAddr();
    if (strcmp(msg->getName(), SELF_SEND_PHOTON) == 0) {
        auto* photon = new quisp::messages::PhotonicQubit("SERVER_PHOTON_LEFT");
        
        send(photon, "toQuantum");
        delete msg;
    }

}



}  // namespace quisp::modules

