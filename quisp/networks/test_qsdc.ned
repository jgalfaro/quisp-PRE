package networks;

import modules.QNode;
import modules.Backend.Backend;
import modules.Logger.Logger;
import modules.SharedResource.SharedResource;
import channels.ClassicalChannel;
import channels.QuantumChannel;

network qsdc_network
{
    parameters:
        @display("bgb=1000,300");

    submodules:
        backend: Backend { @display("p=40,40"); }
        logger: Logger { @display("p=40,90"); }
        sharedResource: SharedResource { @display("p=40,140"); }

        alice: QNode {
            parameters:
                address = 0;
                node_type = "EndNode";
                @display("p=100,150;i=COMP");
        }
        BSArepeater1: QNode {
            parameters:
                address = 1;
                node_type = "QSDCRepeater";
                @display("p=250,150;i=BSA");
        }
        server: QNode {
            parameters:
                address = 2;
                node_type = "QSDCServer"; // Or Repeater, depending on your routing setup
                @display("p=400,150;i=EPPS");
        }
        BSArepeater2: QNode {
            parameters:
                address = 3;
                node_type = "QSDCRepeater";
                @display("p=550,150;i=BSA");
        }
        bob: QNode {
            parameters:
                address = 4;
                node_type = "EndNode";
                @display("p=700,150;i=COMP");
        }

        connections:

                // Alice <--> Repeater1
                alice.port++ <--> ClassicalChannel <--> BSArepeater1.port++;
                alice.quantum_port++ <--> QuantumChannel <--> BSArepeater1.quantum_port++;

                // Repeater1 <--> Server
                BSArepeater1.port++ <--> ClassicalChannel <--> server.port++;
                BSArepeater1.quantum_port++ <--> QuantumChannel <--> server.quantum_port++;

                // Server <--> Repeater2
                server.port++ <--> ClassicalChannel <--> BSArepeater2.port++;
                server.quantum_port++ <--> QuantumChannel <--> BSArepeater2.quantum_port++;

                // Repeater2 <--> Bob
                BSArepeater2.port++ <--> ClassicalChannel <--> bob.port++;
                BSArepeater2.quantum_port++ <--> QuantumChannel <--> bob.quantum_port++;


                // forward path: feft to right (output[1] -> input[0]) 
                alice.appQuantumOut[1] --> QuantumChannel {
                    distance = 2km;
                    channel_loss_rate = 0;
                    channel_x_error_rate = 0;
                    channel_y_error_rate = 0;
                    channel_z_error_rate = 0;
                } --> BSArepeater1.appQuantumIn[0];

                BSArepeater1.appQuantumOut[1] --> QuantumChannel {
                    distance = 2km;
                    // Add error parameters here as needed
                } --> server.appQuantumIn[0];

                server.appQuantumOut[1] --> QuantumChannel {
                    distance = 2km;
                } --> BSArepeater2.appQuantumIn[0];

                BSArepeater2.appQuantumOut[1] --> QuantumChannel {
                    distance = 2km;
                } --> bob.appQuantumIn[0];

                // return path: right to left (output[0] -> input[1]) 
                bob.appQuantumOut[0] --> QuantumChannel {
                    distance = 2km;
                } --> BSArepeater2.appQuantumIn[1];

                BSArepeater2.appQuantumOut[0] --> QuantumChannel {
                    distance = 2km;
                } --> server.appQuantumIn[1];

                server.appQuantumOut[0] --> QuantumChannel {
                    distance = 2km;
                } --> BSArepeater1.appQuantumIn[1];

                BSArepeater1.appQuantumOut[0] --> QuantumChannel {
                    distance = 2km;
                } --> alice.appQuantumIn[1];
}