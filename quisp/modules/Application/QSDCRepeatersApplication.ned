package modules.Application;
@namespace(quisp::modules);

simple QSDCRepeatersApplication
{
  parameters:
    bool is_source = default(false);
    bool is_target = default(false);
    bool is_repeater = default(false);
    bool is_server = default(false);
    bool is_test = default(false);
    
    double custom_channel_loss_rate = default(0.0);
    double custom_measurement_error_rate = default(0.0);
    double custom_gate_error_rate = default(0.0);
    double timeout_interval = default(0.005);


    int total_qubits_to_send = default(20);
    string secret_message = default("Hello World!");
    int expected_bsms = default(1);
    
    // Define exact routing addresses for your topology
    int source_address = default(0);
    int server_address = default(2);
    int target_address = default(4);

    @display("i=block/app");
    int number_of_bellpair;
    int target_addr = default(1);

    int min_pairs_to_start = default(8);
    int sample_target = default(32);
    bool expect_anti = default(false);

    int bell_sample_target = default(16);

    int sample_block_size = default(8);
    int bell_block_size = default(8);

    double start_delay @unit(s) = default(0s);
    double poll_interval @unit(s) = default(0s);
    double sample_interval @unit(s) = default(0s);
    int burn_count;

    double phase1_max_error_rate = default(0.1);
	  double phase2_max_error_rate = default(0.1);

    string payload = default("hello world");

    // Eve settings
    bool eve_enabled = default(false);
    double eve_intercept_probability = default(1.0);

  gates:
    input fromRouter @loose;
    output toRouter @loose;

    input fromQuantum_r @loose;
    input fromQuantum_l @loose;
    
    output toQuantum_l @loose;
    output toQuantum_r @loose;
    
}