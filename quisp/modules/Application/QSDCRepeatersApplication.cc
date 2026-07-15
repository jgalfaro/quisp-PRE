#include "QSDCRepeatersApplication.h"

#include <algorithm>
#include <fstream>
#include <functional>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <omnetpp.h>

#include "modules/QNIC/StationaryQubit/StationaryQubit.h"

// Tim's code to logging
namespace {
#define QLOG(expr)                                                                                             \
  do {                                                                                                         \
    std::ostringstream _qs;                                                                                    \
    _qs << expr;                                                                                               \
    EV_INFO << _qs.str() << "\n";                                                                              \
    /* Keep file logging too if desired */                                                                     \
    std::ofstream logfile("qsdc_app.log", std::ios::app);                                                      \
    if (logfile.is_open()) {                                                                                   \
      logfile << std::fixed << std::setprecision(9) << "[" << omnetpp::simTime() << "] " << _qs.str() << "\n"; \
      logfile.flush();                                                                                         \
    }                                                                                                          \
  } while (0)
}  // namespace

using namespace omnetpp;
using namespace quisp::messages;

namespace quisp::modules {

Define_Module(QSDCRepeatersApplication);

// Custom message values to define each node's behaviors in the protocol

static const char* SELF_GENERATE_PAIRS = "SELF_GENERATE_PAIRS";
static const char* SELF_WAIT_FOR_PAIRS = "SELF_WAIT_FOR_PAIRS";
static const char* SELF_RECEPTION_TIMEOUT = "SELF_RECEPTION_TIMEOUT";
static const char* SELF_QSDC_DEFINE_MESSAGE = "SELF_QSDC_DEFINE_MESSAGE";
static const char* SELF_SEND_PHOTON = "SELF_SEND_PHOTON";
static const char* SELF_QSDC_PREPARE = "SELF_QSDC_PREPARE";
static const char* SELF_QSDC_ENCODE_MESSAGE = "SELF_QSDC_ENCODE_MESSAGE";
static const char* SELF_QKD_DEFINE_PAD = "SELF_QKD_DEFINE_PAD";
static const char* SELF_EXECUTE_TELEPORTATION = "SELF_EXECUTE_TELEPORTATION";

static const char* QPP_TELEPORT_BSM = "QPP_TELEPORT_BSM";
// Protocol messages

static const char* QSDC_MESSAGE_SETUP = "QSDC_MESSAGE_SETUP";
static const char* QSDC_COMM_START = "QSDC_COMM_START";
static const char* QSDC_COMM_READY = "QSDC_COMM_READY";

static const char* QSDC_COMM_SYNC = "QSDC_COMM_SYNC";
static const char* QSDC_COMM_ACK = "QSDC_COMM_ACK";
static const char* QSDC_COMM_END = "QSDC_COMM_END";

static const char* QSDC_QUBIT_SYNC = "QSDC_QUBIT_SYNC";
static const char* QSDC_QUBIT_ACK = "QSDC_QUBIT_ACK";
static const char* QSDC_QUBIT_CONTINUE = "QSDC_QUBIT_CONTINUE";
static const char* QSDC_QUBIT_ERROR = "QSDC_QUBIT_ERROR";
static const char* QSDC_QUBIT_DISCARD = "QSDC_QUBIT_DISCARD";
static const char* QSDC_PURIFY_RESULT = "QSDC_PURIFY_RESULT";
static const char* QSDC_SOURCE_BSM = "QSDC_SOURCE_BSM";

static const char* QKD_BASIS_SYNC = "QKD_BASIS_SYNC";
static const char* QKD_COMPLETED = "QKD_COMPLETED";

void QSDCRepeatersApplication::initialize() {
  std::ofstream logfile("qsdc_app.log", std::ios::trunc);
  if (logfile.is_open()) {
    logfile << "=== Starting New Simulation Run ===\n";
    logfile.close();
  }

  initializeLogger(provider);

  self_address = provider.getNodeAddr();

  auto* qnode = provider.getQNode();
  if (!qnode) {
    QLOG("[QSDC] No QNode found in initialize()");
    return;
  }
  timeout_interval = par("timeout_interval").doubleValue();
  is_source = par("is_source").boolValue();
  is_target = par("is_target").boolValue();
  is_repeater = par("is_repeater").boolValue();
  is_server = par("is_server").boolValue();
  is_eavesdropper = par("is_eavesdropper").boolValue();

  protocol_choice = par("protocol_choice").intValue();

  if (protocol_choice == 1) {
    is_qkd_phase = false;
  } else {
    required_qkd_pad_length = par("required_qkd_pad_length").intValue();
    qubit_block_size = par("qubit_block_size").intValue();
  }

  if (is_eavesdropper) {
    QLOG("[EAVESDROPPER] Eavesdropper activated!");
    entanglement_attack_rate = par("entanglement_attack_rate").doubleValue();

    if (hasPar("targeted_attack_start")) {
      targeted_attack_start = par("targeted_attack_start").intValue();
      targeted_attack_end = par("targeted_attack_end").intValue();
      randomized_malicious_entanglement_attack = par("randomized_malicious_entanglement_attack").boolValue();
    }

    translateAttacks();
  }
  if (hasPar("binary_verification")) {
    binary_verification = par("binary_verification").boolValue();
  }

  source_address = par("source_address").intValue();
  target_address = par("target_address").intValue();
  server_address = par("server_address").intValue();

  if (source_address == target_address || source_address == server_address || target_address == server_address) {
    QLOG("[FAIL] [INIT] Failed while getting network addresses");
    return;
  }

  channel_loss_rate = par("custom_channel_loss_rate").doubleValue();
  measurement_error_rate = par("custom_measurement_error_rate").doubleValue();
  gate_error_rate = par("custom_gate_error_rate").doubleValue();

  if (hasPar("total_qubits_to_send")) {
    total_qubits_to_send = par("total_qubits_to_send").intValue();
  } else {
    total_qubits_to_send = 20;
  }

  if (is_source) {
    if (protocol_choice == 0) {
      scheduleAt(simTime(), new cMessage(SELF_QKD_DEFINE_PAD));
    } else if (protocol_choice == 1) {
      scheduleAt(simTime(), new cMessage(SELF_QSDC_DEFINE_MESSAGE));
    }
  }
}

void QSDCRepeatersApplication::setQKDBits() {
  int num_groups = required_qkd_pad_length;
  int required_raw_pairs = num_groups * 2;  // times 2 because of purification

  QLOG("[SOURCE] Initial QKD Pad bits to be sent were defined : " << required_raw_pairs);

  auto* setup_msg = new QSDCSynAck(QSDC_MESSAGE_SETUP);
  setup_msg->setSrcAddr(self_address);
  setup_msg->setDestAddr(server_address);
  setup_msg->setSequenceNum(required_raw_pairs);
  setup_msg->setFromNode("source");
  send(setup_msg, "toRouter");
}

void QSDCRepeatersApplication::setMessage() {
  secret_message = par("secret_message").stdstringValue();
  QLOG("[SOURCE] Secret Message defined: " << secret_message);

  bit_stream.clear();

  if (binary_verification) {
    QLOG("[SOURCE] Binary Verification Mode Active. Parsing raw bits.");
    for (char c : secret_message) {
      if (c == '0') {
        bit_stream.push_back(0);
      } else if (c == '1') {
        bit_stream.push_back(1);
      } else {
        QLOG("[SOURCE] WARNING: Non-binary character '" << c << "' ignored in binary_verification mode.");
      }
    }
    // QSDC encodes pairs; pad with a 0 if the length is odd
    bit_stream.insert(bit_stream.begin(), 1);

    // If the length is still odd, pad with a leading '0'
    if (bit_stream.size() % 2 != 0) {
      bit_stream.insert(bit_stream.begin(), 0);
      QLOG("[SOURCE] Prepended a '0' and a '1' (start bit) to ensure even length.");
    } else {
      QLOG("[SOURCE] Prepended a '1' (start bit) to the bit stream.");
    }
  } else {
    // Standard ASCII conversion
    for (char c : secret_message) {
      for (int i = 7; i >= 0; --i) {
        bit_stream.push_back((c >> i) & 1);
      }
    }
  }

  int num_groups = bit_stream.size() / 2;
  required_purified_pairs = num_groups * 2;
  int required_raw_pairs = required_purified_pairs * 2;

  QLOG("[SOURCE] Bits: " << bit_stream.size() << " | Req Purified Pairs: " << required_purified_pairs << " | Req Raw Pairs: " << required_raw_pairs);

  auto* setup_msg = new QSDCSynAck(QSDC_MESSAGE_SETUP);
  setup_msg->setSrcAddr(self_address);
  setup_msg->setDestAddr(server_address);
  setup_msg->setSequenceNum(required_raw_pairs);
  setup_msg->setFromNode("source");
  send(setup_msg, "toRouter");
}

void QSDCRepeatersApplication::translateAttacks() {
  chosen_attacks = par("chosen_attacks").intValue();
  // 3 bits:  XXX
  //          1XX = malicious_entanglement
  //          X1X = wrong_bsm_message
  //          XX1 = wrong_hop_count

  if (chosen_attacks & 0b100) {
    attacks.malicious_entanglement = true;
    QLOG("[EAVESDROPPER] Will perform Malicious Entanglement Attack");
  }
  if (chosen_attacks & 0b010) {
    attacks.wrong_bsm_message = true;
    QLOG("[EAVESDROPPER] Will propagate the wrong BSM messages");
  }
  if (chosen_attacks & 0b001) {
    attacks.wrong_hop_count = true;
    QLOG("[EAVESDROPPER] Will propagate the wrong hop count");
  }

  return;
}

void QSDCRepeatersApplication::sendNextQubitPair() {
  QLOG("[SERVER] Emitting Purification Batch: Target Qubit " << current_qubit_index << " & Source Qubit " << current_qubit_index + 1);

  // Reset flags for the next round
  source_received_current = false;
  target_received_current = false;
  source_continue_ready = false;
  target_continue_ready = false;
  server_is_rolling_back = false;

  // Generate TWO pairs instead of one
  auto qubit_pairs = generateEntangledPairs(2, "qnic", 1, BellState::PsiMinus);
  if (qubit_pairs.size() < 2) {
    QLOG("[SERVER] ERROR: Quantum Memory Exhaustion. Cannot generate 2 pairs.");
    return;
  }

  server_emitted_qubits[current_qubit_index] = {qubit_pairs[0].qubit_1, qubit_pairs[0].qubit_2};
  server_emitted_qubits[current_qubit_index + 1] = {qubit_pairs[1].qubit_1, qubit_pairs[1].qubit_2};

  // Send Target Qubit (Index K)
  sendClassicalMessage(source_address, QSDC_QUBIT_SYNC, "QSDC_QUBIT_SYNC", current_qubit_index);
  sendClassicalMessage(target_address, QSDC_QUBIT_SYNC, "QSDC_QUBIT_SYNC", current_qubit_index);

  auto* photon_left_0 = new quisp::messages::PhotonicQubit("SERVER_PHOTON_L_TARGET");
  photon_left_0->setQubitRef(qubit_pairs[0].qubit_1->getBackendQubitRef());
  photon_left_0->addPar("sequence_number") = current_qubit_index;
  photon_left_0->addPar("direction") = "left";

  auto* photon_right_0 = new quisp::messages::PhotonicQubit("SERVER_PHOTON_R_TARGET");
  photon_right_0->setQubitRef(qubit_pairs[0].qubit_2->getBackendQubitRef());
  photon_right_0->addPar("sequence_number") = current_qubit_index;
  photon_right_0->addPar("direction") = "right";

  send(photon_left_0, "toQuantum_l");
  send(photon_right_0, "toQuantum_r");

  // Send Source Qubit (Index K + 1)
  sendClassicalMessage(source_address, QSDC_QUBIT_SYNC, "QSDC_QUBIT_SYNC", current_qubit_index + 1);
  sendClassicalMessage(target_address, QSDC_QUBIT_SYNC, "QSDC_QUBIT_SYNC", current_qubit_index + 1);

  auto* photon_left_1 = new quisp::messages::PhotonicQubit("SERVER_PHOTON_L_SOURCE");
  photon_left_1->setQubitRef(qubit_pairs[1].qubit_1->getBackendQubitRef());
  photon_left_1->addPar("sequence_number") = current_qubit_index + 1;
  photon_left_1->addPar("direction") = "left";

  auto* photon_right_1 = new quisp::messages::PhotonicQubit("SERVER_PHOTON_R_SOURCE");
  photon_right_1->setQubitRef(qubit_pairs[1].qubit_2->getBackendQubitRef());
  photon_right_1->addPar("sequence_number") = current_qubit_index + 1;
  photon_right_1->addPar("direction") = "right";

  send(photon_left_1, "toQuantum_l");
  send(photon_right_1, "toQuantum_r");
}

void QSDCRepeatersApplication::sendClassicalMessage(int dest_addr, const char* msg_type, const char* msg_name, int seq_num, int meas_res) {
  auto* pkt = new QSDCSynAck(msg_name);
  pkt->setSrcAddr(self_address);
  pkt->setDestAddr(dest_addr);
  pkt->setName(msg_type);

  if (is_source)
    pkt->setFromNode("source");
  else if (is_target)
    pkt->setFromNode("target");
  else if (is_server)
    pkt->setFromNode("server");
  else
    pkt->setFromNode("repeater");

  pkt->setSequenceNum(seq_num);

  pkt->setMeasResult(meas_res);

  QLOG("[MESSAGE] Sending ClassicalMessage " << msg_name << " to " << ((dest_addr == source_address) ? "Source" : (dest_addr == target_address) ? "Target" : "Server"));

  send(pkt, "toRouter");
}

void QSDCRepeatersApplication::processQubitSync(quisp::messages::QSDCSynAck* pkt) {
  if (is_repeater) {
    send(pkt, "toRouter");
    return;
  }

  int seq_num = pkt->getSequenceNum();
  QLOG("[" << (is_source ? "SOURCE" : "TARGET") << "] Server marked Qubit Index: " << seq_num << " in transit.");

  if (is_source || is_target) {
    // Since qubits are emitted in pairs (Target=even, Source=odd), track the base Target index
    if (seq_num % 2 == 0) {
      current_qubit_index = seq_num;
    }
  }

  // Start or reset the timeout clock
  if (qubit_reception_timeout_msg != nullptr) {
    cancelAndDelete(qubit_reception_timeout_msg);
  }
  qubit_reception_timeout_msg = new cMessage(SELF_RECEPTION_TIMEOUT);
  scheduleAt(simTime() + timeout_interval, qubit_reception_timeout_msg);

  delete pkt;
}

void QSDCRepeatersApplication::processReceptionTimeout(omnetpp::cMessage* msg) {
  QLOG("[" << (is_source ? "SOURCE" : "TARGET") << "] FATAL: Timeout waiting for Qubits or Purification results.");

  // Alert the Server to discard and roll back the current sequence
  sendClassicalMessage(server_address, QSDC_QUBIT_ERROR, "QSDC_QUBIT_ERROR", current_qubit_index);

  qubit_reception_timeout_msg = nullptr;
  delete msg;
}

void QSDCRepeatersApplication::protocolInit() {
  // override the standard connection manager for testing the physical layer swap
  if (is_initiator) {
    QLOG("[QSDC Source] Bypassing Connection Manager for direct Server test.");
    // delay long enough for Server to generate, Repeater to swap, and Target to apply corrections
    scheduleAt(simTime() + par("sample_interval"), new cMessage(SELF_WAIT_FOR_PAIRS));
  }
}

std::vector<LocalBellPair> QSDCRepeatersApplication::generateEntangledPairs(int n, const char* qnic_type, int qnic_index, BellState state) {
  QLOG("[QSDC PairGen] Generating Entangled Pairs...");
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
      break;  // We have identified enough memory slots
    }
  }

  // atomic allocation check
  if (free_indices.size() < 2 * n) {
    QLOG("[QSDC PairGen] Insufficient memory. Requested " << n << " pairs (" << 2 * n << " qubits), but only " << free_indices.size() << " available in QNIC.");
    return generated_pairs;  // caller must handle resource exhaustion
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
      QLOG("[QSDC PairGen] Generating local Bell pair across qi=" << qi_1 << " and qi=" << qi_2 << " mapped to state: Phi+");
      // |00> + |11> (Base state, no operations needed)
    } else if (state == BellState::PhiMinus) {
      QLOG("[QSDC PairGen] Generating local Bell pair across qi=" << qi_1 << " and qi=" << qi_2 << " mapped to state: Phi-");
      // |00> - |11>
      qubit_1->gateZ();
    } else if (state == BellState::PsiPlus) {
      QLOG("[QSDC PairGen] Generating local Bell pair across qi=" << qi_1 << " and qi=" << qi_2 << " mapped to state: Psi+");
      // |01> + |10>
      qubit_2->gateX();
    } else if (state == BellState::PsiMinus) {  // Actually used on the protocol
      QLOG("[QSDC PairGen] Generating local Bell pair across qi=" << qi_1 << " and qi=" << qi_2 << " mapped to state: Psi-");
      // |01> - |10>
      qubit_1->gateZ();
      qubit_2->gateX();
    }

    applyDepolarizingNoise(qubit_1->getBackendQubitRef());
    applyDepolarizingNoise(qubit_2->getBackendQubitRef());

    // Pack the physical references into the return struct
    generated_pairs.push_back({qnic_index, qi_1, qi_2, qubit_1, qubit_2});
  }

  return generated_pairs;
}

void QSDCRepeatersApplication::applyDepolarizingNoise(quisp::backends::IQubit* qubit) {
  if (dblrand() < gate_error_rate) {
    // Randomly choose X, Y, or Z error (33% chance each given an error occurs)
    double err_type = dblrand();
    if (err_type < 0.33) {
      qubit->gateX();
      QLOG("[ERROR INJECTION] Depolarizing noise applied: X-Gate");
    } else if (err_type < 0.66) {
      qubit->gateZ();
      QLOG("[ERROR INJECTION] Depolarizing noise applied: Z-Gate");
    } else {
      qubit->gateX();
      qubit->gateZ();  // Simulating Y-Gate (iY)
      QLOG("[ERROR INJECTION] Depolarizing noise applied: Y-Gate");
    }
  }
}

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
  std::string state_str = "";

  if (z0_result == +1 && z1_result == +1) {
    state_str = "Phi+";
    bsm_outcome = 0;
  }  // Phi+
  else if (z0_result == -1 && z1_result == +1) {
    state_str = "Phi-";
    bsm_outcome = 1;
  }  // Phi-
  else if (z0_result == +1 && z1_result == -1) {
    state_str = "Psi+";
    bsm_outcome = 2;
  }  // Psi+
  else if (z0_result == -1 && z1_result == -1) {
    state_str = "Psi-";
    bsm_outcome = 3;
  }  // Psi-

  QLOG("[QSDC BSM] Repeater Swapping Qubit " << seq_num << " | Meas Z0: " << (z0_result == 1 ? "+1" : "-1") << " | Meas Z1: " << (z1_result == 1 ? "+1" : "-1")
                                             << " | Projected State: " << state_str << " | Outcome ID: " << bsm_outcome);

  local_qubit->Unlock();

  if (is_eavesdropper && attacks.wrong_bsm_message) {
    QLOG("[EAVESDROPPER] Sending a wrong BSM to endnode");
    bsm_outcome = ((int)(10 * dblrand()) + bsm_outcome) % 4;  // randomizes the BSM to the endnode (umpredictable outcome)
  }

  if (dst_addr != -1) {
    QSDCBSMResult* bsm_packet = new QSDCBSMResult("BSM_Announcement");
    bsm_packet->setSrcAddr(self_address);
    bsm_packet->setDestAddr(dst_addr);
    bsm_packet->setBsmOutcome(bsm_outcome);
    bsm_packet->setSequenceNum(seq_num);
    send(bsm_packet, "toRouter");
  }
}

void QSDCRepeatersApplication::handleBSMResult(int seq_num, int bsm_outcome) {
  if (received_qubits.find(seq_num) == received_qubits.end()) {
    QLOG("[WARNING] BSM arrived before photon for sequence: " << seq_num);
    return;
  }

  auto* qubit = received_qubits[seq_num];

  // outcome 0 (Phi+): State became Phi+. Needs XZ (iY) to return to Psi-
  if (bsm_outcome == 0) {
    cumulative_corrections[seq_num].apply_x ^= true;
    cumulative_corrections[seq_num].apply_z ^= true;
  }
  // outcome 1 (Phi-): State became Phi-. Needs X to return to Psi-
  else if (bsm_outcome == 1) {
    cumulative_corrections[seq_num].apply_x ^= true;
  }
  // outcome 2 (Psi+): State became Psi+. Needs Z to return to Psi-
  else if (bsm_outcome == 2) {
    cumulative_corrections[seq_num].apply_z ^= true;
  }
  // outcome 3 (Psi-): State remained Psi-. No correction needed (I gate).
  else if (bsm_outcome == 3) {
  }

  bsm_arrival_counts[seq_num]++;

  QLOG("[" << (is_source ? "SOURCE" : "TARGET") << "] Received BSM for Qubit " << seq_num << " | Progress: " << bsm_arrival_counts[seq_num] << "/" << expected_bsms_count);

  int target_bsm_count = (expected_bsms_count > 0) ? expected_bsms_count : par("expected_bsms").intValue();

  if (bsm_arrival_counts[seq_num] == target_bsm_count) {
    std::string correction_applied = "";
    if (cumulative_corrections[seq_num].apply_x) {
      qubit->gateX();
      correction_applied += "X ";
    }
    if (cumulative_corrections[seq_num].apply_z) {
      qubit->gateZ();
      correction_applied += "Z ";
    }
    if (correction_applied == "") {
      correction_applied = "I (None)";
    }

    QLOG("[" << (is_source ? "SOURCE" : "TARGET") << "] All BSMs received for Qubit " << seq_num << ". Applying cumulative quantum frame correction: " << correction_applied);

    cumulative_corrections.erase(seq_num);

    ready_qubits.push_back(seq_num);
    attemptPurification();
  }
}

omnetpp::cModule* QSDCRepeatersApplication::getQNIC(const char* qnic_type, int qnic_index) {
  auto* qnode = provider.getQNode();
  if (!qnode) return nullptr;

  // qnic_type will be "qnic", "qnic_r", or "qnic_rp"
  return qnode->getSubmodule(qnic_type, qnic_index);
}

void QSDCRepeatersApplication::attemptPurification() {
  if (ready_qubits.size() < 2) {
    QLOG("[" << (is_source ? "SOURCE" : "TARGET") << "] Only " << ready_qubits.size() << " qubit(s) ready. Waiting for a complete pair.");
    return;
  }

  int target_seq = -1;
  int source_seq = -1;

  for (int seq : ready_qubits) {
    if (seq % 2 == 0) {
      auto partner_it = std::find(ready_qubits.begin(), ready_qubits.end(), seq + 1);
      if (partner_it != ready_qubits.end()) {
        target_seq = seq;
        source_seq = seq + 1;
        break;
      }
    }
  }

  if (target_seq == -1) {
    QLOG("[" << (is_source ? "SOURCE" : "TARGET") << "] Ready queue has qubits, but no matching Target/Source pair. Waiting.");
    return;
  }

  ready_qubits.erase(std::remove(ready_qubits.begin(), ready_qubits.end(), target_seq), ready_qubits.end());
  ready_qubits.erase(std::remove(ready_qubits.begin(), ready_qubits.end(), source_seq), ready_qubits.end());

  auto* target_qubit = received_qubits[target_seq];
  auto* source_qubit = received_qubits[source_seq];

  QLOG("[" << (is_source ? "SOURCE" : "TARGET") << "] Executing Purification. Target: " << target_seq << ", Source: " << source_seq);

  if (is_source) {
    target_qubit->gateX();
    target_qubit->gateZ();
    source_qubit->gateX();
    source_qubit->gateZ();
  }

  target_qubit->gateCNOT(source_qubit);

  applyDepolarizingNoise(target_qubit);
  applyDepolarizingNoise(source_qubit);

  int meas_res = eigenToInt(source_qubit->measureZ());

  my_local_measurements[target_seq] = meas_res;
  QLOG("[" << (is_source ? "SOURCE" : "TARGET") << "] Z-Measurement Result for Target Qubit " << target_seq << ": " << meas_res);

  source_qubit->setFree();
  received_qubits.erase(source_seq);

  int partner_address = is_source ? target_address : source_address;

  // Send our normal purification packet
  auto* purification_packet = new QSDCPurificationResult("Purify_result");
  purification_packet->setSrcAddr(self_address);
  purification_packet->setDestAddr(partner_address);
  purification_packet->setPurificationResult(meas_res);
  purification_packet->setSequenceNum(target_seq);
  send(purification_packet, "toRouter");

  // BUG FIX: Check if the partner's classical packet arrived while we were calculating
  if (early_partner_meas.find(target_seq) != early_partner_meas.end()) {
    int early_meas = early_partner_meas[target_seq];
    early_partner_meas.erase(target_seq);

    QLOG("[" << (is_source ? "SOURCE" : "TARGET") << "] Retrieving early purification packet from buffer.");

    // Re-create the classical packet and inject it into the OMNeT++ event queue
    auto* buffered_pkt = new QSDCPurificationResult("Purify_result_buffered");
    buffered_pkt->setSrcAddr(partner_address);
    buffered_pkt->setDestAddr(self_address);
    buffered_pkt->setPurificationResult(early_meas);
    buffered_pkt->setSequenceNum(target_seq);

    // scheduleAt(simTime()) tells OMNeT++ to route this packet back into handleMessage immediately
    scheduleAt(simTime(), buffered_pkt);
  }
}

void QSDCRepeatersApplication::handleIncomingPhotonAtEndNode(quisp::messages::PhotonicQubit* photon) {
  if (dblrand() < channel_loss_rate) {
    QLOG("[ERROR INJECTION] Photon lost before reaching EndNode.");
    delete photon;
    return;
  }

  int seq_num = (int)photon->par("sequence_number").longValue();
  QLOG("[ENDNODE] Received PhotonicQubit sequence: " << seq_num << ". Attempting local storage.");

  if (is_source || is_target) {
    auto* qnic = getQNIC("qnic", 0);
    if (!qnic) {
      QLOG("[ENDNODE] FATAL: Receiver QNIC not found!");
      delete photon;
      return;
    }

    quisp::modules::StationaryQubit* local_sq = nullptr;
    const int num_buf = qnic->par("num_buffer").intValue();

    for (int i = 0; i < num_buf; i++) {
      auto* sq = check_and_cast<quisp::modules::StationaryQubit*>(qnic->getSubmodule("statQubit", i));
      if (!sq->isBusy() && !sq->isLocked()) {
        local_sq = sq;
        local_sq->setBusy();  // Lock local memory
        break;
      }
    }

    if (!local_sq) {
      QLOG("[ENDNODE] FATAL: Out of local quantum memory. Cannot store sequence " << seq_num);
      delete photon;
      return;
    }

    auto* incoming_qubit = const_cast<quisp::backends::IQubit*>(photon->getQubitRef());
    auto* local_backend_qubit = local_sq->getBackendQubitRef();
    // 3-CNOT SWAP Gate
    incoming_qubit->gateCNOT(local_backend_qubit);
    local_backend_qubit->gateCNOT(incoming_qubit);
    incoming_qubit->gateCNOT(local_backend_qubit);

    received_qubits[seq_num] = local_backend_qubit;

    local_stored_qubits[seq_num] = local_sq;

    QLOG("[ENDNODE] Qubit " << seq_num << " successfully absorbed into local memory. Sending STORED ACK.");
  }

  delete photon;
}
void QSDCRepeatersApplication::handleIncomingPhotonAtRepeater(quisp::messages::PhotonicQubit* photon) {
  if (dblrand() < channel_loss_rate) {
    QLOG("[ERROR INJECTION] Photon " << photon->par("sequence_number").longValue() << " lost in channel.");
    delete photon;
    return;
  }

  QLOG("[REPEATER ES] Processing Photon to perform the Entanglement Swap...");
  auto new_pairs = generateEntangledPairs(1, "qnic", 1, BellState::PsiMinus);

  if (new_pairs.empty()) {
    QLOG("[REPEATER] FATAL: Insufficient quantum memory to generate ES pair. Dropping photon.");
    delete photon;
    return;
  }

  int seq_num = (int)photon->par("sequence_number").longValue();
  std::string photon_direction = photon->par("direction").stringValue();

  repeater_emitted_qubits[seq_num] = {new_pairs[0].qubit_1, new_pairs[0].qubit_2};

  auto* local_half = new_pairs[0].qubit_1;
  auto* remote_half = new_pairs[0].qubit_2;
  int dst_addr = (photon_direction == "left") ? par("source_address").intValue() : par("target_address").intValue();
  bool is_targeted_sequence = (seq_num >= targeted_attack_start && seq_num <= targeted_attack_end);

  if (is_eavesdropper && attacks.malicious_entanglement && is_targeted_sequence && !randomized_malicious_entanglement_attack) {
    // Track direction and measurement per sequence to prevent OMNeT++ overwriting
    eve_attack_direction[seq_num] = photon_direction;

    auto* incoming_qubit = const_cast<quisp::backends::IQubit*>(photon->getQubitRef());
    eve_intercept_meas[seq_num] = eigenToInt(incoming_qubit->measureZ());

    int eve_fake_meas = eigenToInt(local_half->measureZ());
    if (eve_fake_meas == 1) {
      remote_half->gateX();
    }

    local_half->setFree(true);

    QLOG("[EAVESDROPPER] Collapsed Server photon " << seq_num << " to " << eve_intercept_meas[seq_num] << ". Forwarding |0>.");

    auto* next_photon = new quisp::messages::PhotonicQubit("FORWARDED_PHOTON");
    next_photon->setQubitRef(remote_half->getBackendQubitRef());
    next_photon->addPar("src_addr") = self_address;
    next_photon->addPar("qubit_index") = remote_half;
    next_photon->addPar("sequence_number") = seq_num;
    next_photon->addPar("direction") = photon_direction.c_str();

    if (photon_direction == "left") send(next_photon, "toQuantum_l");
    if (photon_direction == "right") send(next_photon, "toQuantum_r");

    if (dst_addr != -1) {
      QSDCBSMResult* bsm_packet = new QSDCBSMResult("BSM_Announcement");
      bsm_packet->setSrcAddr(self_address);
      bsm_packet->setDestAddr(dst_addr);
      bsm_packet->setBsmOutcome(3);  // Force BSM outcome 3 to prevent EndNode Pauli corrections
      bsm_packet->setSequenceNum(seq_num);
      send(bsm_packet, "toRouter");
    }

    delete photon;
    return;
  } else if (is_eavesdropper && attacks.malicious_entanglement && randomized_malicious_entanglement_attack && dblrand() < entanglement_attack_rate) {
    eve_attack_direction[seq_num] = photon_direction;

    auto* incoming_qubit = const_cast<quisp::backends::IQubit*>(photon->getQubitRef());
    eve_intercept_meas[seq_num] = eigenToInt(incoming_qubit->measureZ());

    int eve_fake_meas = eigenToInt(local_half->measureZ());
    if (eve_fake_meas == 1) {
      remote_half->gateX();
    }

    local_half->setFree(true);

    QLOG("[EAVESDROPPER] Collapsed Server photon " << seq_num << " to " << eve_intercept_meas[seq_num] << ". Forwarding |0>.");

    auto* next_photon = new quisp::messages::PhotonicQubit("FORWARDED_PHOTON");
    next_photon->setQubitRef(remote_half->getBackendQubitRef());
    next_photon->addPar("src_addr") = self_address;
    next_photon->addPar("qubit_index") = remote_half;
    next_photon->addPar("sequence_number") = seq_num;
    next_photon->addPar("direction") = photon_direction.c_str();

    if (photon_direction == "left") send(next_photon, "toQuantum_l");
    if (photon_direction == "right") send(next_photon, "toQuantum_r");

    if (dst_addr != -1) {
      QSDCBSMResult* bsm_packet = new QSDCBSMResult("BSM_Announcement");
      bsm_packet->setSrcAddr(self_address);
      bsm_packet->setDestAddr(dst_addr);
      bsm_packet->setBsmOutcome(3);  // Force BSM outcome 3 to prevent EndNode Pauli corrections
      bsm_packet->setSequenceNum(seq_num);
      send(bsm_packet, "toRouter");
    }

    delete photon;
    return;
  } else {
    auto* incoming_qubit = const_cast<quisp::backends::IQubit*>(photon->getQubitRef());

    measureBellStateAndSend(incoming_qubit, local_half, dst_addr, seq_num);

    local_half->setFree(true);

    auto* next_photon = new quisp::messages::PhotonicQubit("FORWARDED_PHOTON");
    next_photon->setQubitRef(remote_half->getBackendQubitRef());
    next_photon->addPar("src_addr") = self_address;
    next_photon->addPar("qubit_index") = remote_half;
    next_photon->addPar("sequence_number") = seq_num;
    next_photon->addPar("direction") = photon_direction.c_str();

    if (photon_direction == "left") send(next_photon, "toQuantum_l");
    if (photon_direction == "right") send(next_photon, "toQuantum_r");

    delete photon;
  }
}

void QSDCRepeatersApplication::cleanupRepeaterMemory() {
  QLOG("[REPEATER CLEANUP] Cleaning all tracked repeater QNIC allocations.");

  for (auto const& [seq_num, qubits] : repeater_emitted_qubits) {
    for (auto* sq : qubits) {
      if (sq) {
        sq->setFree(true);
      }
    }
  }

  repeater_emitted_qubits.clear();
}

// Utility mapper for eigenvalues
int QSDCRepeatersApplication::eigenToInt(quisp::backends::abstract::EigenvalueResult r) {
  int expected_result = (r == quisp::backends::abstract::EigenvalueResult::PLUS_ONE) ? +1 : -1;
  if (dblrand() < measurement_error_rate) {
    QLOG("[ERROR INJECTION] Measurement error! Flipping Z-basis result.");
    return (expected_result == +1) ? -1 : +1;
  }

  return expected_result;
}

void QSDCRepeatersApplication::encodeAndPerformQSDC() {
  std::sort(stored_purified_qubit_seqs.begin(), stored_purified_qubit_seqs.end());
  int bit_index = 0;

  for (size_t i = 0; i < stored_purified_qubit_seqs.size(); i += 2) {
    if (bit_index >= bit_stream.size()) break;

    int q1_seq = stored_purified_qubit_seqs[i];  // B1
    int q2_seq = stored_purified_qubit_seqs[i + 1];  // B2

    auto* qubit_1 = received_qubits[q1_seq];
    auto* qubit_2 = received_qubits[q2_seq];

    int bit1 = bit_stream[bit_index++];
    int bit2 = bit_stream[bit_index++];

    if (bit1 == 0 && bit2 == 0) {
      // I gate
    } else if (bit1 == 0 && bit2 == 1) {
      qubit_1->gateZ();
    } else if (bit1 == 1 && bit2 == 0) {
      qubit_1->gateX();
    } else if (bit1 == 1 && bit2 == 1) {
      qubit_1->gateZ();
      qubit_1->gateX();
    }

    qubit_1->gateCNOT(qubit_2);
    qubit_1->gateH();

    int z1 = eigenToInt(qubit_1->measureZ());
    int z2 = eigenToInt(qubit_2->measureZ());

    // Encode BSM result (0: ++, 1: +-, 2: -+, 3: --)
    int bsm_outcome = (z1 == -1 ? 2 : 0) + (z2 == -1 ? 1 : 0);

    QLOG("[SOURCE] QSDC Group " << (i / 2) << " Encoded bits: " << bit1 << bit2 << " | BSM Outcome: " << bsm_outcome);

    auto* qsdc_msg = new QSDCBSMResult(QSDC_SOURCE_BSM);
    qsdc_msg->setSrcAddr(self_address);
    qsdc_msg->setDestAddr(target_address);  // Target
    qsdc_msg->setSequenceNum(i / 2);  // Group Index
    qsdc_msg->setBsmOutcome(bsm_outcome);
    send(qsdc_msg, "toRouter");

    qubit_1->setFree();
    qubit_2->setFree();
  }
}

void QSDCRepeatersApplication::decodeQSDC() {
  QLOG("[TARGET] Starting Deterministic Batch Decoding in decodeQSDC().");

  std::sort(stored_purified_qubit_seqs.begin(), stored_purified_qubit_seqs.end());

  decoded_bit_stream.clear();
  decoded_bit_stream.resize(buffered_source_bsms.size() * 2);

  for (auto const& [group_index, source_bsm] : buffered_source_bsms) {
    int q1_seq = stored_purified_qubit_seqs[group_index * 2];
    int q2_seq = stored_purified_qubit_seqs[group_index * 2 + 1];

    auto* qubit_1 = received_qubits[q1_seq];
    auto* qubit_2 = received_qubits[q2_seq];

    qubit_1->gateCNOT(qubit_2);
    qubit_1->gateH();

    int z1 = eigenToInt(qubit_1->measureZ());
    int z2 = eigenToInt(qubit_2->measureZ());
    int target_bsm = (z1 == -1 ? 2 : 0) + (z2 == -1 ? 1 : 0);

    int xor_val = source_bsm ^ target_bsm;
    int bit1 = 0, bit2 = 0;

    if (xor_val == 0) {
      bit1 = 0;
      bit2 = 0;
    }  // I gate
    else if (xor_val == 2) {
      bit1 = 0;
      bit2 = 1;
    }  // Z gate
    else if (xor_val == 1) {
      bit1 = 1;
      bit2 = 0;
    }  // X gate
    else if (xor_val == 3) {
      bit1 = 1;
      bit2 = 1;
    }  // iY gate

    // Map directly to the exact index, bypassing OMNeT++ arrival order
    decoded_bit_stream[group_index * 2] = bit1;
    decoded_bit_stream[group_index * 2 + 1] = bit2;

    if (binary_verification) {
      QLOG("[TARGET] Decoded QSDC Group " << group_index << " -> Bits: " << bit1 << bit2);
    }

    qubit_1->setFree();
    qubit_2->setFree();
  }

  // Final message reconstruction branch
  if (binary_verification) {
    std::string final_binary_message = "";
    for (int b : decoded_bit_stream) {
      final_binary_message += std::to_string(b);
    }

    size_t start_bit_pos = final_binary_message.find('1');
    if (start_bit_pos != std::string::npos) {
      final_binary_message = final_binary_message.substr(start_bit_pos + 1);
    }

    QLOG("[TARGET] FINAL RECONSTRUCTED BINARY: " << final_binary_message);
  } else {
    std::string final_message = "";
    for (size_t i = 0; i + 7 < decoded_bit_stream.size(); i += 8) {
      char c = 0;
      for (int j = 0; j < 8; ++j) {
        c = (c << 1) | decoded_bit_stream[i + j];
      }
      final_message += c;
    }
    QLOG("[TARGET] FINAL RECONSTRUCTED MESSAGE: " << final_message);
  }
}

void QSDCRepeatersApplication::checkAndTriggerDecoding() {
  if (!comm_end_received) return;

  size_t expected_groups = stored_purified_qubit_seqs.size() / 2;

  if (buffered_source_bsms.size() == expected_groups && expected_groups > 0) {
    decodeQSDC();
  } else {
    QLOG("[TARGET] COMM_END received, but waiting for Source's encoding BSMs. Progress: " << buffered_source_bsms.size() << " / " << expected_groups);
  }
}

void QSDCRepeatersApplication::handleMessage(cMessage* msg) {
  if (auto* photon = dynamic_cast<quisp::messages::PhotonicQubit*>(msg)) {
    if (is_repeater) {
      handleIncomingPhotonAtRepeater(photon);
    } else if (is_source || is_target) {
      // Both QKD and QSDC require local storage to wait for BSM corrections
      handleIncomingPhotonAtEndNode(photon);
    } else {
      delete photon;
    }
    return;
  }

  if (auto* hop_msg = dynamic_cast<quisp::messages::QSDCHopMessage*>(msg)) {
    if (is_repeater) {
      if (is_eavesdropper && attacks.wrong_hop_count) {
        QLOG("[EAVESDROPPER] Sending wrong hop count to endnode");
        hop_msg->setQSDCHopCount(hop_msg->getQSDCHopCount());
      } else
        hop_msg->setQSDCHopCount(hop_msg->getQSDCHopCount() + 1);
      QLOG("[REPEATER] Intercepted Hop Probe. Incremented counter to: " << hop_msg->getQSDCHopCount());
      send(hop_msg, "toRouter");
    } else if (is_source || is_target) {
      expected_bsms_count = hop_msg->getQSDCHopCount();
      QLOG("[" << (is_source ? "SOURCE" : "TARGET") << "] Hop Probe arrived! Dynamic Expected BSMs set to: " << expected_bsms_count);
      sendClassicalMessage(hop_msg->getSrcAddr(), "QSDC_COMM_READY", "QSDC_COMM_READY");
      delete hop_msg;
    }
    return;
  }

  if (auto* bsm_msg = dynamic_cast<QSDCBSMResult*>(msg)) {
    if (strcmp(bsm_msg->getName(), QSDC_SOURCE_BSM) == 0) {
      // Buffer the BSMs
      buffered_source_bsms[bsm_msg->getSequenceNum()] = bsm_msg->getBsmOutcome();
      if (is_target) {
        checkAndTriggerDecoding();
      }

    } else {
      handleBSMResult(bsm_msg->getSequenceNum(), bsm_msg->getBsmOutcome());
    }

    delete msg;
    return;
  }

  if (strcmp(msg->getName(), SELF_EXECUTE_TELEPORTATION) == 0) {
    executeTeleportation();
    delete msg;
    return;
  }

  // String-Based cMessages
  if (strcmp(msg->getName(), SELF_QSDC_PREPARE) == 0) {
    processQSDCPrepare(msg);
    return;
  }

  if (strcmp(msg->getName(), QSDC_COMM_START) == 0) {
    processCommStart(msg);
    return;
  }
  if (strcmp(msg->getName(), SELF_QSDC_DEFINE_MESSAGE) == 0) {
    // Triggered at initialization by Source
    setMessage();
    delete msg;
    return;
  }

  if (strcmp(msg->getName(), SELF_QKD_DEFINE_PAD) == 0) {
    // Triggered at initialization by Source
    setQKDBits();
    delete msg;
    return;
  }

  if (strcmp(msg->getName(), SELF_QSDC_ENCODE_MESSAGE) == 0) {
    // Triggered by Source once all required pairs are purified
    encodeAndPerformQSDC();
    delete msg;
    return;
  }

  if (strcmp(msg->getName(), SELF_QSDC_PREPARE) == 0) {
    processQSDCPrepare(msg);
    return;
  }

  if (strcmp(msg->getName(), QSDC_COMM_START) == 0) {
    processCommStart(msg);
    return;
  }

  if (strcmp(msg->getName(), SELF_RECEPTION_TIMEOUT) == 0) {
    processReceptionTimeout(msg);
    return;
  }

  if (auto* pkt = dynamic_cast<quisp::messages::QSDCCleanQuantumMemory*>(msg)) {
    if (is_repeater) {
      cleanupRepeaterMemory();
      send(pkt, "toRouter");
      return;
    }

    delete msg;
    return;
  }
  if (auto* pkt = dynamic_cast<quisp::messages::QSDCPurificationResult*>(msg)) {
    processPurifyResult(pkt);
    return;
  }

  // QSDC FSM Classical Packets
  if (auto* pkt = dynamic_cast<quisp::messages::QSDCSynAck*>(msg)) {
    std::string msg_type = pkt->getName();

    if (msg_type == QSDC_MESSAGE_SETUP)
      processMessageSetup(pkt);
    else if (msg_type == QSDC_COMM_SYNC)
      processCommSync(pkt);
    else if (msg_type == QSDC_COMM_END)
      processCommEnd(pkt);
    else if (msg_type == QSDC_COMM_READY)
      processCommReady(pkt);
    else if (msg_type == QSDC_COMM_ACK)
      processCommAck(pkt);
    else if (msg_type == QSDC_QUBIT_ACK)
      processQubitAck(pkt);
    else if (msg_type == QSDC_QUBIT_SYNC)
      processQubitSync(pkt);
    else if (msg_type == QSDC_QUBIT_ERROR)
      processQubitError(pkt);
    else if (msg_type == QSDC_QUBIT_DISCARD)
      processQubitDiscard(pkt);
    else if (msg_type == QSDC_QUBIT_CONTINUE)
      processQubitContinue(pkt);
    else if (msg_type == QKD_BASIS_SYNC)
      processQKDBasisSync(pkt);
    else if (msg_type == QPP_TELEPORT_BSM)
      processTeleportBSM(pkt);
    else if (msg_type == QKD_COMPLETED)
      processQKDCompleted(pkt);
    else {
      if (is_repeater)
        send(msg, "toRouter");  // Forward unrecognized if repeater
      else
        delete msg;
    }

    return;
  }

  delete msg;
}

void QSDCRepeatersApplication::processMessageSetup(quisp::messages::QSDCSynAck* pkt) {
  if (is_server) {
    total_qubits_to_send = pkt->getSequenceNum();
    int dynamic_source_address = pkt->getSrcAddr();

    // Initialize the Probes
    auto* probe_source = new QSDCHopMessage("QSDC_COMM_START");
    probe_source->setSrcAddr(self_address);
    probe_source->setDestAddr(dynamic_source_address);
    probe_source->setQSDCHopCount(0);
    send(probe_source, "toRouter");

    auto* probe_target = new QSDCHopMessage("QSDC_COMM_START");
    probe_target->setSrcAddr(self_address);
    probe_target->setDestAddr(target_address);  // Ensure Target's address is known dynamically as well
    probe_target->setQSDCHopCount(0);
    send(probe_target, "toRouter");
  }
  delete pkt;
}

void QSDCRepeatersApplication::processQSDCPrepare(omnetpp::cMessage* msg) {
  QLOG("[SERVER] Initializing QSDC Protocol. Requesting EndNode Readiness.");
  sendClassicalMessage(source_address, QSDC_COMM_START, "QSDC_COMM_START");
  sendClassicalMessage(target_address, QSDC_COMM_START, "QSDC_COMM_START");
  delete msg;
}

void QSDCRepeatersApplication::processCommStart(omnetpp::cMessage* msg) {
  auto* pkt = dynamic_cast<quisp::messages::QSDCSynAck*>(msg);
  if (!pkt) return;

  if (is_repeater) {
    // Increment the hop count dynamically
    int current_hops = pkt->getExpectedBSMs();
    pkt->setExpectedBSMs(current_hops + 1);

    QLOG("[REPEATER] Forwarding CommStart. Updated hop count to: " << current_hops + 1);
    send(pkt, "toRouter");
    return;
  }

  // Logic for Source and Target
  expected_bsms_count = pkt->getExpectedBSMs();
  QLOG("[" << (is_source ? "SOURCE" : "TARGET") << "] Received Start. Dynamic Expected BSMs set to: " << expected_bsms_count);

  sendClassicalMessage(server_address, QSDC_COMM_READY, "QSDC_COMM_READY");
  delete msg;
}

void QSDCRepeatersApplication::processCommSync(quisp::messages::QSDCSynAck* pkt) {
  if (is_repeater) {
    send(pkt, "toRouter");
    return;
  }
  QLOG("[" << (is_source ? "SOURCE" : "TARGET") << "] Session Synced. Buffer cleared. Sending ACK.");
  received_qubits.clear();
  ready_qubits.clear();
  bsm_arrival_counts.clear();

  if (is_target) {
    decoded_bit_stream.clear();
  }

  // Tell the Server we are safely cleared and ready
  sendClassicalMessage(server_address, QSDC_COMM_ACK, "QSDC_COMM_ACK");  // 2 is Server
  delete pkt;
}

void QSDCRepeatersApplication::processCommEnd(quisp::messages::QSDCSynAck* pkt) {
  QLOG("[" << (is_source ? "SOURCE" : "TARGET") << "] PROTOCOL COMPLETE! End Signal Received.");

  if (protocol_choice == 0) { // QPP Protocol Branch
    if (is_qkd_phase) {
      if (is_source || is_target) {
        doQKD();
      }
    } else {
      QLOG("[" << (is_source ? "SOURCE" : "TARGET") << "] Teleportation Block Distribution Complete.");

      if (is_target) {
        QLOG("[TARGET] Generating Un-Shuffle Map at synchronized FSM barrier.");
        target_shuffled_seqs = stored_purified_qubit_seqs;
        std::sort(target_shuffled_seqs.begin(), target_shuffled_seqs.end());
        
        std::mt19937 prng(deterministic_seed);
        std::shuffle(target_shuffled_seqs.begin(), target_shuffled_seqs.end(), prng);
      } else if (is_source) {
        QLOG("[SOURCE] Triggering Quantum Pad Permutation (Teleportation) at FSM barrier.");
        cMessage* teleport_msg = new cMessage(SELF_EXECUTE_TELEPORTATION);
        scheduleAt(simTime() + par("sample_interval"), teleport_msg);
      }
    }
  } 
  else if (protocol_choice == 1) { // Standard QSDC Branch
    if (is_target) {
      comm_end_received = true;
      checkAndTriggerDecoding();
    } else if (is_source) {
      // Also shift QSDC encoding to the safe barrier
      QLOG("[SOURCE] Initiating QSDC Encoding at synchronized FSM barrier.");
      scheduleAt(simTime() + par("sample_interval"), new cMessage(SELF_QSDC_ENCODE_MESSAGE));
    }
  }
  delete pkt;
}

void QSDCRepeatersApplication::processCommReady(quisp::messages::QSDCSynAck* pkt) {
  if (is_repeater) {
    send(pkt, "toRouter");
    return;
  }
  if (is_server) {
    std::string from = pkt->getFromNode();
    if (from == "source") source_ready = true;
    if (from == "target") target_ready = true;

    if (source_ready && target_ready) {
      QLOG("[SERVER] Both nodes ready. Sending Sync to clear buffers.");

      // Reset flags for the upcoming ACK phase
      source_ready = false;
      target_ready = false;

      sendClassicalMessage(source_address, QSDC_COMM_SYNC, "QSDC_COMM_SYNC");
      sendClassicalMessage(target_address, QSDC_COMM_SYNC, "QSDC_COMM_SYNC");
    }
  }
  delete pkt;
}

void QSDCRepeatersApplication::processCommAck(quisp::messages::QSDCSynAck* pkt) {
  if (is_repeater) {
    send(pkt, "toRouter");
    return;
  }

  if (is_repeater) {
    send(pkt, "toRouter");
    return;
  }

  if (is_server) {
    std::string from = pkt->getFromNode();
    if (from == "source") source_ready = true;
    if (from == "target") target_ready = true;

    if (source_ready && target_ready) {
      QLOG("[SERVER] EndNodes synced. Starting Qubit Emission FSM.");

      source_ready = false;
      target_ready = false;

      current_qubit_index = 0;
      sendNextQubitPair();
    }
  }
  delete pkt;
}

void QSDCRepeatersApplication::processQubitAck(quisp::messages::QSDCSynAck* pkt) {
  if (is_server) {
    int rcv_index = pkt->getSequenceNum();
    std::string from = pkt->getFromNode();

    if (rcv_index == current_qubit_index) {  // Source/Target ACK the Target index
      if (from == "source") source_received_current = true;
      if (from == "target") target_received_current = true;

      if (source_received_current && target_received_current) {
        QLOG("[SERVER] Target Qubit " << current_qubit_index << " Successfully Purified & Synchronized!");

        if (server_emitted_qubits.find(current_qubit_index) != server_emitted_qubits.end()) {
          for (auto* sq : server_emitted_qubits[current_qubit_index]) sq->setFree(true);
          for (auto* sq : server_emitted_qubits[current_qubit_index + 1]) sq->setFree(true);
          server_emitted_qubits.erase(current_qubit_index);
          server_emitted_qubits.erase(current_qubit_index + 1);
        }

        auto* clear_repeater_memories_source_msg = new QSDCCleanQuantumMemory(QSDC_QUBIT_DISCARD);
        clear_repeater_memories_source_msg->setSrcAddr(self_address);
        clear_repeater_memories_source_msg->setDestAddr(source_address);
        send(clear_repeater_memories_source_msg, "toRouter");

        auto* clear_repeater_memories_target_msg = new QSDCCleanQuantumMemory(QSDC_QUBIT_DISCARD);
        clear_repeater_memories_target_msg->setSrcAddr(self_address);
        clear_repeater_memories_target_msg->setDestAddr(target_address);
        send(clear_repeater_memories_target_msg, "toRouter");

        // advance by 2
        current_qubit_index += 2;

        if (current_qubit_index < total_qubits_to_send) {
          sendNextQubitPair();
        } else {
          QLOG("[SERVER] Transmission Complete. Sending END signal.");
          sendClassicalMessage(source_address, QSDC_COMM_END, "COMM_END");
          sendClassicalMessage(target_address, QSDC_COMM_END, "COMM_END");
        }
      }
    }
  }
  delete pkt;
}

void QSDCRepeatersApplication::processPurifyResult(quisp::messages::QSDCPurificationResult* pkt) {
  if (qubit_reception_timeout_msg != nullptr) {
    cancelAndDelete(qubit_reception_timeout_msg);
    qubit_reception_timeout_msg = nullptr;
  }

  if (pkt->getDestAddr() != self_address) {
    if (is_repeater && is_eavesdropper && attacks.malicious_entanglement) {
      int target_seq = pkt->getSequenceNum();
      int source_seq = target_seq + 1;
      int dest = pkt->getDestAddr();

      if (eve_attack_direction.find(target_seq) != eve_attack_direction.end()) {
        std::string direction = eve_attack_direction[target_seq];

        if ((direction == "right" && dest == target_address) || (direction == "left" && dest == source_address)) {
          QLOG("[EAVESDROPPER] Spoofing packet to Attacked node -> +1");
          pkt->setPurificationResult(1);
        } else if (eve_intercept_meas.find(target_seq) != eve_intercept_meas.end() && eve_intercept_meas.find(source_seq) != eve_intercept_meas.end()) {
          int m0 = eve_intercept_meas[target_seq];
          int m1 = eve_intercept_meas[source_seq];
          int predicted_meas = (m0 == m1) ? 1 : -1;

          QLOG("[EAVESDROPPER] Predicting Un-attacked node state -> Spoofing to " << predicted_meas);
          pkt->setPurificationResult(predicted_meas);
        }
      }
    }
    send(pkt, "toRouter");
    return;
  }

  int target_seq = pkt->getSequenceNum();
  int partner_meas = pkt->getPurificationResult();

  if (my_local_measurements.find(target_seq) == my_local_measurements.end()) {
    QLOG("[" << (is_source ? "SOURCE" : "TARGET") << "] Packet arrived before local measurement. Waiting.");

    delete pkt;
    return;
  }

  int my_meas = my_local_measurements[target_seq];
  QLOG("[" << (is_source ? "SOURCE" : "TARGET") << "] Comparing Purification Results. Mine: " << my_meas << ", Partner: " << partner_meas);

if (my_meas == partner_meas) {
    QLOG("[" << (is_source ? "SOURCE" : "TARGET") << "] Purification SUCCESS for Qubit " << target_seq);

    if (is_source) {
      received_qubits[target_seq]->gateZ();
      received_qubits[target_seq]->gateX();
    }

    stored_purified_qubit_seqs.push_back(target_seq);
    sendClassicalMessage(server_address, QSDC_QUBIT_ACK, "QSDC_QUBIT_ACK", target_seq);

  } else {
    QLOG("[" << (is_source ? "SOURCE" : "TARGET") << "] Purification FAILED for Qubit " << target_seq << ". State compromised. Initiating ARQ.");

    cleanupLocalMemory(target_seq);
    cleanupLocalMemory(target_seq + 1);

    sendClassicalMessage(server_address, QSDC_QUBIT_ERROR, "QSDC_QUBIT_ERROR", target_seq);
  }

  delete pkt;
}

void QSDCRepeatersApplication::processQubitError(quisp::messages::QSDCSynAck* pkt) {
  if (is_repeater) {
    send(pkt, "toRouter");
    return;
  }
  if (is_server) {
    int err_index = pkt->getSequenceNum();
    if (err_index == current_qubit_index && !server_is_rolling_back) {
      QLOG("[SERVER] ARQ: Error reported on Qubit " << err_index << ". Issuing DISCARD to both nodes.");
      if (server_emitted_qubits.find(current_qubit_index) != server_emitted_qubits.end()) {
        for (auto* sq : server_emitted_qubits[current_qubit_index]) sq->setFree(true);
        for (auto* sq : server_emitted_qubits[current_qubit_index + 1]) sq->setFree(true);
        server_emitted_qubits.erase(current_qubit_index);
        server_emitted_qubits.erase(current_qubit_index + 1);
      }
      server_is_rolling_back = true;
      source_continue_ready = false;
      target_continue_ready = false;

      auto* clear_repeater_memories_source_msg = new QSDCCleanQuantumMemory(QSDC_QUBIT_DISCARD);
      clear_repeater_memories_source_msg->setSrcAddr(self_address);
      clear_repeater_memories_source_msg->setDestAddr(source_address);
      send(clear_repeater_memories_source_msg, "toRouter");

      auto* clear_repeater_memories_target_msg = new QSDCCleanQuantumMemory(QSDC_QUBIT_DISCARD);
      clear_repeater_memories_target_msg->setSrcAddr(self_address);
      clear_repeater_memories_target_msg->setDestAddr(target_address);
      send(clear_repeater_memories_target_msg, "toRouter");

      sendClassicalMessage(source_address, QSDC_QUBIT_DISCARD, "QSDC_QUBIT_DISCARD", current_qubit_index);
      sendClassicalMessage(target_address, QSDC_QUBIT_DISCARD, "QSDC_QUBIT_DISCARD", current_qubit_index);
    }
  }
  delete pkt;
}

void QSDCRepeatersApplication::processQubitDiscard(quisp::messages::QSDCSynAck* pkt) {
  if (qubit_reception_timeout_msg != nullptr) {
    cancelAndDelete(qubit_reception_timeout_msg);
    qubit_reception_timeout_msg = nullptr;
  }

  // The EndNodes execute the local memory scrub
  if (is_source || is_target) {
    int target_index = pkt->getSequenceNum();
    int source_index = target_index + 1;  // The partner qubit in the purification batch

    QLOG("[" << (is_source ? "SOURCE" : "TARGET") << "] ARQ: Discarding Batch Qubit Indices: " << target_index << " & " << source_index);

    cleanupLocalMemory(target_index);
    cleanupLocalMemory(source_index);

    ready_qubits.erase(std::remove(ready_qubits.begin(), ready_qubits.end(), target_index), ready_qubits.end());
    ready_qubits.erase(std::remove(ready_qubits.begin(), ready_qubits.end(), source_index), ready_qubits.end());

    bsm_arrival_counts.erase(target_index);
    bsm_arrival_counts.erase(source_index);

    stored_purified_qubit_seqs.erase(std::remove(stored_purified_qubit_seqs.begin(), stored_purified_qubit_seqs.end(), target_index), stored_purified_qubit_seqs.end());
    stored_purified_qubit_seqs.erase(std::remove(stored_purified_qubit_seqs.begin(), stored_purified_qubit_seqs.end(), source_index), stored_purified_qubit_seqs.end());

    cumulative_corrections.erase(target_index);
    cumulative_corrections.erase(source_index);

    my_local_measurements.erase(target_index);
    my_local_measurements.erase(source_index);

    QLOG("[" << (is_source ? "SOURCE" : "TARGET") << "] ARQ: Local state scrubbed. QNIC slots freed. Sending CONTINUE to Server.");
    sendClassicalMessage(server_address, QSDC_QUBIT_CONTINUE, "QSDC_QUBIT_CONTINUE", target_index);
  }

  delete pkt;
}

void QSDCRepeatersApplication::cleanupLocalMemory(int seq_num) {
  if (local_stored_qubits.find(seq_num) != local_stored_qubits.end()) {
    if (local_stored_qubits[seq_num] != nullptr) {
      local_stored_qubits[seq_num]->setFree(true);
    }
    local_stored_qubits.erase(seq_num);
  }

  if (received_qubits.find(seq_num) != received_qubits.end()) {
    received_qubits.erase(seq_num);
  }
  return;
}

void QSDCRepeatersApplication::processQubitContinue(quisp::messages::QSDCSynAck* pkt) {
  if (is_repeater) {
    send(pkt, "toRouter");
    return;
  }
  if (is_server) {
    int cont_index = pkt->getSequenceNum();
    std::string from = pkt->getFromNode();

    if (cont_index == current_qubit_index) {
      if (from == "source") source_continue_ready = true;
      if (from == "target") target_continue_ready = true;

      if (source_continue_ready && target_continue_ready) {
        QLOG("[SERVER] ARQ: Both nodes discarded successfully. Re-emitting Qubit " << current_qubit_index);
        sendNextQubitPair();
      }
    }
  }
  delete pkt;
}

void QSDCRepeatersApplication::processQKDBasisSync(quisp::messages::QSDCSynAck* pkt) {
  std::map<int, int> source_bases;
  std::string basis_str = pkt->par("qkd_basis_choices").stringValue();

  std::stringstream ss(basis_str);
  std::string item;

  // Parse the "seq:basis," string
  while (std::getline(ss, item, ',')) {
    if (item.empty()) continue;
    size_t colon_pos = item.find(':');
    if (colon_pos != std::string::npos) {
      int seq = std::stoi(item.substr(0, colon_pos));
      int basis = std::stoi(item.substr(colon_pos + 1));
      source_bases[seq] = basis;
    }
  }

  QLOG("[QKD TARGET] Received Basis Sync containing " << source_bases.size() << " basis choices.");
  QLOG("[QKD TARGET] Received Basis Sync containing " << source_bases.size() << " basis choices.");

  std::string matched_seqs = "";

  for (auto const& [seq_num, source_basis] : source_bases) {
    if (qkd_basis_choices.find(seq_num) != qkd_basis_choices.end()) {
      int target_basis = qkd_basis_choices[seq_num];

      if (source_basis == target_basis) {
        int my_meas = qkd_measurement_results[seq_num];
        private_pad.push_back(my_meas);

        matched_seqs += std::to_string(seq_num) + ",";

        QLOG("[QKD TARGET] Sifting SUCCESS on Qubit " << seq_num << ". Basis: " << target_basis << ". Pad Bit added.");
      } else {
        QLOG("[QKD TARGET] Sifting MISMATCH on Qubit " << seq_num << ". Discarding.");
      }

      qkd_basis_choices.erase(seq_num);
      qkd_measurement_results.erase(seq_num);
    }
  }

  QLOG("[QKD TARGET] Current Private Pad Length: " << private_pad.size() << " / " << required_qkd_pad_length);

  QLOG("[QKD TARGET] Private Pad established! Transitioning to QSDC Phase.");
  is_qkd_phase = false;

  stored_purified_qubit_seqs.clear();

  printPad();

  // 1st Message: Tell Server the Target is done
  sendClassicalMessage(server_address, "QKD_COMPLETED", "QKD_COMPLETED");

  // 2nd Message: Send Serialized Match Data to Source
  auto* src_comp_msg = new QSDCSynAck("QKD_COMPLETED");
  src_comp_msg->addPar("sifted_sequences").setStringValue(matched_seqs.c_str());
  src_comp_msg->setSrcAddr(self_address);
  src_comp_msg->setDestAddr(source_address);
  src_comp_msg->setFromNode("target");

  QLOG("[MESSAGE] Sending ClassicalMessage QKD_COMPLETED with sifted sequence data to Source");
  send(src_comp_msg, "toRouter");

  delete pkt;
}

void QSDCRepeatersApplication::printPad() {
  if (private_pad.empty()) {
    QLOG("[QKD " << (is_source ? "SOURCE" : "TARGET") << "] ERROR: Private Pad is empty.");
    return;
  }

  std::string pad_str = "";
  for (int bit : private_pad) {
    pad_str += (bit == -1) ? "1" : "0";
  }

  std::hash<std::string> hasher;
  deterministic_seed = hasher(pad_str);


  QLOG("[QKD " << (is_source ? "SOURCE" : "TARGET") << "] FINAL SECURE PAD GENERATED: " << pad_str);
  QLOG("[QKD " << (is_source ? "SOURCE" : "TARGET") << "] DETERMINISTIC PERMUTATION SEED: " << deterministic_seed);
}

void QSDCRepeatersApplication::processQKDCompleted(quisp::messages::QSDCSynAck* pkt) {
  if (is_source) {
    QLOG("[QKD SOURCE] Received QKD_COMPLETED signal from Target. Locking QKD phase.");
    is_qkd_phase = false;
    stored_purified_qubit_seqs.clear();

    // Deserialize the sifted sequence indices
    if (pkt->hasPar("sifted_sequences")) {
      std::string matched_str = pkt->par("sifted_sequences").stringValue();
      std::stringstream ss(matched_str);
      std::string item;

      while (std::getline(ss, item, ',')) {
        if (item.empty()) continue;
        int seq = std::stoi(item);

        // Populate the Source pad based on the Target's successful matches
        if (qkd_measurement_results.find(seq) != qkd_measurement_results.end()) {
          private_pad.push_back(qkd_measurement_results[seq] * -1);
        }
      }
    }

    // Source prints its pad
    printPad();

    // Source notifies the Server that it is finished
    sendClassicalMessage(server_address, QKD_COMPLETED, "QKD_COMPLETED");

  } else if (is_server) {
    // Reusing FSM flags to track EndNode completions
    std::string from = pkt->getFromNode();
    if (from == "source") source_ready = true;
    if (from == "target") target_ready = true;

    if (source_ready && target_ready) {
      QLOG("[SERVER] QKD Phase Complete. Transitioning to Quantum Pad Permutation (QPP) Phase.");

      source_ready = false;
      target_ready = false;

      stored_purified_qubit_seqs.clear();
      current_qubit_index = 0;

      total_qubits_to_send = qubit_block_size;  // prepare to send the block

      sendNextQubitPair();
    }
  }

  delete pkt;
}

void QSDCRepeatersApplication::doQKD() {
  QLOG("[" << (is_source ? "SOURCE" : "TARGET") << "] Executing Batch QKD Measurements on stored pairs...");

  std::sort(stored_purified_qubit_seqs.begin(), stored_purified_qubit_seqs.end());

  for (int seq_num : stored_purified_qubit_seqs) {
    if (received_qubits.find(seq_num) == received_qubits.end()) continue;

    auto* purified_qubit = received_qubits[seq_num];

    // randomly choose a basis: 0 for Z, 1 for X
    int basis = (dblrand() < 0.5) ? 0 : 1;
    int meas_res = 0;

    if (basis == 0) {
      meas_res = eigenToInt(purified_qubit->measureZ());
    } else {
      meas_res = eigenToInt(purified_qubit->measureX());
    }

    qkd_basis_choices[seq_num] = basis;
    qkd_measurement_results[seq_num] = meas_res;

    QLOG("[QKD] Purified Qubit " << seq_num << " measured in " << (basis == 0 ? "Z" : "X") << "-basis. Result: " << meas_res);

    cleanupLocalMemory(seq_num);
  }

  if (is_source) {
    auto* qkd_sync_message = new QSDCSynAck(QKD_BASIS_SYNC);
    std::string basis_str = "";

    for (auto const& [seq, stored_basis] : qkd_basis_choices) {
      basis_str += std::to_string(seq) + ":" + std::to_string(stored_basis) + ",";
    }

    qkd_sync_message->addPar("qkd_basis_choices").setStringValue(basis_str.c_str());
    qkd_sync_message->setSrcAddr(self_address);
    qkd_sync_message->setDestAddr(target_address);
    send(qkd_sync_message, "toRouter");

    QLOG("[QKD SOURCE] Batch measurement complete. Sent Basis Sync to Target.");
  }
}

void QSDCRepeatersApplication::executeTeleportation() {
  int transmission_index = 0;
  QLOG("[SOURCE] Initiating Quantum Pad Permutation: Teleporting random block.");

  std::sort(stored_purified_qubit_seqs.begin(), stored_purified_qubit_seqs.end());

  applyQuantumPadPermutation();

  auto* qnic = getQNIC("qnic", 0);
  if (!qnic) {
    QLOG("[SOURCE] FATAL: Local QNIC not found for Data Block generation.");
    return;
  }

  const int num_buf = qnic->par("num_buffer").intValue();

  for (int seq_num : stored_purified_qubit_seqs) {
    if (received_qubits.find(seq_num) == received_qubits.end()) continue;

    auto* epr_half = received_qubits[seq_num];

    quisp::modules::StationaryQubit* data_sq = nullptr;
    for (int i = 0; i < num_buf; i++) {
      auto* sq = check_and_cast<quisp::modules::StationaryQubit*>(qnic->getSubmodule("statQubit", i));
      if (!sq->isBusy() && !sq->isLocked()) {
        data_sq = sq;
        data_sq->setBusy();
        break;
      }
    }

    if (!data_sq) {
      QLOG("[SOURCE] WARNING: QNIC memory exhausted during teleportation allocation.");
      break;
    }

    auto* data_qubit = data_sq->getBackendQubitRef();

    int prep_basis = (dblrand() < 0.5) ? 0 : 1;  // 0 = Z-basis, 1 = X-basis
    int expected_eigenvalue = (dblrand() < 0.5) ? 1 : -1;  // 1 = |0> / |+>, -1 = |1> / |->

    if (prep_basis == 0) {  // Z-Basis Preparation
      if (expected_eigenvalue == -1) data_qubit->gateX();  // Prepare |1>
    } else {  // X-Basis Preparation
      data_qubit->gateH();  // Prepare |+>
      if (expected_eigenvalue == -1) data_qubit->gateZ();  // Prepare |->
    }

    QLOG("[SOURCE] Prepared verification qubit " << seq_num << " | Basis: " << (prep_basis == 0 ? "Z" : "X") << " | Expected Z/X Eigenvalue: " << expected_eigenvalue);

    data_qubit->gateCNOT(epr_half);
    data_qubit->gateH();

    int m1 = eigenToInt(data_qubit->measureZ()) == 1 ? 0 : 1;
    int m2 = eigenToInt(epr_half->measureZ()) == 1 ? 0 : 1;

    int bsm_outcome = (m1 << 1) | m2;

    QLOG("[SOURCE] BSM on sequence " << seq_num << " | m1(D): " << m1 << " m2(A): " << m2 << " -> Code: " << bsm_outcome);

    auto* tel_msg = new QSDCSynAck(QPP_TELEPORT_BSM);
    tel_msg->setSrcAddr(self_address);
    tel_msg->setDestAddr(target_address);

    tel_msg->setSequenceNum(transmission_index++);

    tel_msg->setMeasResult(bsm_outcome);
    tel_msg->addPar("prep_basis") = prep_basis;
    tel_msg->addPar("expected_eigenvalue") = expected_eigenvalue;

    send(tel_msg, "toRouter");

    data_sq->setFree(true);
    cleanupLocalMemory(seq_num);
  }
}

  void QSDCRepeatersApplication::processTeleportBSM(quisp::messages::QSDCSynAck * pkt) {
    if (is_target) {
      int masked_index = pkt->getSequenceNum();

      if (masked_index >= target_shuffled_seqs.size()) {
        QLOG("[TARGET] FATAL: Out-of-bounds masked index received.");
        delete pkt;
        return;
      }
      int seq_num = target_shuffled_seqs[masked_index];

      if (received_qubits.find(seq_num) == received_qubits.end()) {
        QLOG("[TARGET] ERROR: EPR Half for teleportation sequence " << seq_num << " not found in memory.");
        delete pkt;
        return;
      }

      auto* target_epr = received_qubits[seq_num];
      int bsm_outcome = pkt->getMeasResult();

      if (received_qubits.find(seq_num) == received_qubits.end()) {
        QLOG("[TARGET] ERROR: EPR Half for teleportation sequence " << seq_num << " not found in memory.");
        delete pkt;
        return;
      }

      int m1 = (bsm_outcome >> 1) & 1;
      int m2 = bsm_outcome & 1;

      QLOG("[TARGET] Received Teleport BSM for sequence " << seq_num << ". Applying Pauli Frame (m1=" << m1 << ", m2=" << m2 << ")");

      // Apply Psi- Teleportation Pauli Corrections
      if (m1 == 0 && m2 == 0) {
        target_epr->gateX();
      } else if (m1 == 0 && m2 == 1) {
        // Identity - No operation needed
      } else if (m1 == 1 && m2 == 0) {
        target_epr->gateZ();
        target_epr->gateX();
      } else if (m1 == 1 && m2 == 1) {
        target_epr->gateZ();
      }

      QLOG("[TARGET] Teleportation of Data Qubit " << seq_num << " complete. State recovered and preserved in pure form.");

      if (local_stored_qubits.find(seq_num) != local_stored_qubits.end()) {
        pure_teleported_qubits[seq_num] = local_stored_qubits[seq_num];
        local_stored_qubits.erase(seq_num);
        received_qubits.erase(seq_num);
      }

      stored_purified_qubit_seqs.erase(std::remove(stored_purified_qubit_seqs.begin(), stored_purified_qubit_seqs.end(), seq_num), stored_purified_qubit_seqs.end());

      if (stored_purified_qubit_seqs.empty()) {
        QLOG("[TARGET] ==============================================================");
        QLOG("[TARGET] Quantum Pad Permutation (QPP) successfully completed.");
        QLOG("[TARGET] Entire block teleported safely. Pure states are locked in pure_teleported_qubits.");
        QLOG("[TARGET] ==============================================================");
      }
    }
    delete pkt;
  }

void QSDCRepeatersApplication::applyQuantumPadPermutation() {
  if (stored_purified_qubit_seqs.empty()) {
    QLOG("[QPP] WARNING: Cannot permute. EPR sequence array is empty.");
    return;
  }

  std::mt19937 prng(deterministic_seed);

  std::shuffle(stored_purified_qubit_seqs.begin(), stored_purified_qubit_seqs.end(), prng);
  
  QLOG("[QPP SOURCE] Memory mapped and permuted successfully using shared seed: " << deterministic_seed);
}

}  // namespace quisp::modules
