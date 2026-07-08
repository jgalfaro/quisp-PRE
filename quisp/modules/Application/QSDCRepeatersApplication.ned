package modules.Application;
@namespace(quisp::modules);

simple QSDCRepeatersApplication
{
  parameters:
    bool is_source = default(false);
    bool is_target = default(false);
    bool is_repeater = default(false);
    bool is_server = default(false);
    
    double custom_channel_loss_rate = default(0.0);
    double custom_measurement_error_rate = default(0.0);
    double custom_gate_error_rate = default(0.0);
    double timeout_interval = default(0.005);

    bool is_eavesdropper = default(false);
    int chosen_attacks = default(0);
    double entanglement_attack_rate = default(0.0);
    int total_qubits_to_send = default(20);
    string secret_message = default("Hello World!");
    int expected_bsms = default(1);
    bool binary_verification = default(false);
    int targeted_attack_start = default(0);
    int targeted_attack_end = default(0);
    bool randomized_malicious_entanglement_attack = default(false);
    // Define exact routing addresses for your topology
    int source_address = default(0);
    int server_address = default(2);
    int target_address = default(4);

    @display("i=block/app");

    double start_delay @unit(s) = default(0s);
    double poll_interval @unit(s) = default(0s);
    double sample_interval @unit(s) = default(0s);

  gates:
    input fromRouter @loose;
    output toRouter @loose;

    input fromQuantum_r @loose;
    input fromQuantum_l @loose;
    
    output toQuantum_l @loose;
    output toQuantum_r @loose;
    
}