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
        @display("bgb=700,250");

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
        repeater1: QNode {
            parameters:
                address = 1;
                node_type = "Repeater";
                @display("p=250,150;i=ROUTER");
        }
        server: QNode {
            parameters:
                address = 2;
                node_type = "Server"; // Or Repeater, depending on your routing setup
                @display("p=400,150;i=SERVER");
        }
        repeater2: QNode {
            parameters:
                address = 3;
                node_type = "Repeater";
                @display("p=550,150;i=ROUTER");
        }
        bob: QNode {
            parameters:
                address = 4;
                node_type = "EndNode";
                @display("p=700,150;i=COMP");
        }

    connections:
        // Alice <--> Repeater1
        alice.port++ <--> ClassicalChannel <--> repeater1.port++;
        alice.quantum_port++ <--> QuantumChannel <--> repeater1.quantum_port++;

        // Repeater1 <--> Server (Generates Server qnic[0])
        repeater1.port++ <--> ClassicalChannel <--> server.port++;
        repeater1.quantum_port++ <--> QuantumChannel <--> server.quantum_port++;

        // Server <--> Repeater2 (Generates Server qnic[1])
        server.port++ <--> ClassicalChannel <--> repeater2.port++;
        server.quantum_port++ <--> QuantumChannel <--> repeater2.quantum_port++;

        // Repeater2 <--> Bob
        repeater2.port++ <--> ClassicalChannel <--> bob.port++;
        repeater2.quantum_port++ <--> QuantumChannel <--> bob.quantum_port++;
}