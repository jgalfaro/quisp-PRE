import base_messages;
namespace quisp::messages;

// Packet representing Bob's classical announcement of his BSM outcome
packet QSDCBSMResult extends Header {
    // RuleSet and Rule ID to identify the operation to be done 
    unsigned long ruleset_id @setter(setRulesetId)         @getter(getRulesetId);
    int shared_rule_tag      @setter(setSharedRuleTag)     @getter(getSharedRuleTag);

    // Identifies the specific group of EPR pairs being used for this 2-bit chunk
    int sequence_num         @setter(setSequenceNum)       @getter(getSequenceNum);
    
    // The Bell-basis measurement outcome on photons (e.g., 0=Phi+, 1=Phi-, 2=Psi+, 3=Psi-)
    int bsm_outcome          @setter(setBsmOutcome)        @getter(getBsmOutcome); 
}

// Packet handling all QSDC Classical Synchronization, ARQ, and Purification
packet QSDCSynAck extends Header {
    // RuleSet and Rule ID to identify the operation to be done 
    unsigned long ruleset_id @setter(setRulesetId)         @getter(getRulesetId);
    int shared_rule_tag      @setter(setSharedRuleTag)     @getter(getSharedRuleTag);

    // Identifies who sent the message ("alice", "bob", "server")
    string from_node         @setter(setFromNode)          @getter(getFromNode);
    
    // Tracks the Qubit Index (K) for Sync, Error, Discard, and Continue states
    int sequence_num         @setter(setSequenceNum)       @getter(getSequenceNum);

    // Stores the Z-basis measurement outcome (+1 or -1) for Application-Layer Purification
    int meas_result          @setter(setMeasResult)        @getter(getMeasResult);

    int expected_bsms        @setter(setExpectedBSMs)        @getter(getExpectedBSMs);
}