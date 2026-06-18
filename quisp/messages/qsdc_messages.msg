// /home/caio/Downloads/omnetpp-6.0.3/bin/opp_msgc ./quisp_qsdc/quisp/messages/qsdc_messages.msg 
import base_messages;
namespace quisp::messages;

// Packet representing Bob's classical announcement of his BSM outcome

packet QSDCBSMResult extends Header {
    // RuleSet and Rule ID to identuify the operation to be done 
    unsigned long ruleset_id @setter(setRulesetId)         @getter(getRulesetId);
    int shared_rule_tag      @setter(setSharedRuleTag)     @getter(getSharedRuleTag);

    // Identifies the specific group of EPR pairs being used for this 2-bit chunk
    int sequence_num             @setter(setSequenceNum)           @getter(getSequenceNum);
    
    // Bob's Bell-basis measurement outcome on photons B1 and B2
    // Typically represented as an integer mapping to Phi+, Phi-, Psi+, Psi-
    int bsm_outcome          @setter(setBsmOutcome)        @getter(getBsmOutcome); 

}

packet QSDCSynAck extends Header {
    // RuleSet and Rule ID to identuify the operation to be done 
    unsigned long ruleset_id @setter(setRulesetId)         @getter(getRulesetId);
    int shared_rule_tag      @setter(setSharedRuleTag)     @getter(getSharedRuleTag);

}


