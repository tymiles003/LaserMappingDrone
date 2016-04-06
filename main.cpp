//
// Created by Galen on 1/13/16.
//

#include <iostream>
#include <SDL_timer.h>
#include <SDL_events.h>
#include <SDL_thread.h>

#include "Graphics.h"
#include "Grid.h"
#include "GridDrawer.h"
#include "readerwriterqueue.h"
#include "PacketAnalyzer.h"
#include "PacketReceiver.h"

#define SCROLL_ZOOM_SENSITIVITY 0.08f
#define KEY_ZOOM_EXTRA_SENSITIVITY 0.005f
#define KEY_MOVE_SENSITIVITY 0.2f

using namespace LaserMappingDrone;

// These regard the handling of incoming data from the LIDAR device
PacketAnalyzer* analyzer;
PacketReceiver* receiver;
bool packetHandlerQuit;
SDL_Thread* packetListeningThread;

// The grid and drawer
// constructor min/max arguments are in meters (the LIDAR device is at the origin)
//Grid<CartesianPoint> grid(-1.f, 1.f, -1.f, 1.f, 10, 10, 1000000);
Grid<CartesianPoint> grid(-3.f, 3.f, -3.f, 3.f, 10, 10, 500000);
GridDrawer<CartesianPoint> gridDrawer;

// The graphics backend
Graphics graphics;

// This is a thread safe queue designed for one producer and one consumer
moodycamel::ReaderWriterQueue<CartesianPoint> queue(10000);

// Some things helpful to controls
long double zoomLevel;
unsigned previousTime, currentTime, deltaTime; // Used to regulate controls time step

/* Flag Booleans: */
bool graphicsModeEnabled, streamModeEnabled, writeModeEnabled;
StreamingMedium streamMedium;
std::string dataFileName;



// prototypes
void mainLoop();
void initPacketHandling(std::string filename);
void stopPacketHandling();
void initKernel();
int initGraphics();
int handleControls();
int listeningThreadFunction(void* listeningThreadData);
void enableGraphicsMode();
void disableGraphicsMode();
void enableStreamMode();
void disableStreamMode();
void getStreamDevice();
void enableWriteMode();
void disableWriteMode();

int main(int argc, char* argv[]) {
    /* Deal with the command line flags: */
    if (argc == 1) {
        /* User gave no flags, so prompt the user for the flags they would like to use: */
        char input = '0';
        /* Check for graphics flag: */
        while (input != 'y' && input != 'Y' && input != 'n' && input != 'N') {
            std::cout << "Enable graphical display? (y/n) ";
            std::cin.get(input);
            std::cin.ignore(256, '\n');
            if (input == 'y' || input == 'Y') {
                enableGraphicsMode();
            } else if (input == 'n' || input == 'N') {
                disableGraphicsMode();
            } else {
                std::cout << "Please enter either 'y' or 'n'." << std::endl;
            }
        }
        input = '0';
        while (input != 'y' && input != 'Y' && input != 'n' && input != 'N') {
            std::cout << "Enable data streaming? (y/n) ";
            std::cin.get(input);
            std::cin.ignore(256, '\n');
            if (input == 'y' || input == 'Y') {
                enableStreamMode();
            } else if (input == 'n' || input == 'N') {
                disableStreamMode();
            } else {
                std::cout << "Please enter either 'y' or 'n'." << std::endl;
            }
        }
        if (streamModeEnabled) {
            while (input != 'y' && input != 'Y' && input != 'n' && input != 'N') {
                std::cout << "Enable writing data to a file? (y/n) ";
                std::cin.get(input);
                std::cin.ignore(256, '\n');
                if (input == 'y' || input == 'Y') {
                    enableWriteMode();
                } else if (input == 'n' || input == 'N') {
                    disableWriteMode();
                } else {
                    std::cout << "Please enter either 'y' or 'n'." << std::endl;
                }
            }
        }
    } else {
        /* Handle the user specified flags: */
        /* Default flag values are set to false: */
        graphicsModeEnabled = false;
        streamModeEnabled = false;
        writeModeEnabled = false;
        for (int fc = 1; fc < argc; ++fc) {
            if (std::string(argv[fc]) == "-g") {
                enableGraphicsMode();
            } else if (std::string(argv[fc]) == "-s") {
                enableStreamMode();
            } else if (std::string(argv[fc]) == "-sw"){
                enableStreamMode();
                enableWriteMode();
            } else if (std::string(argv[fc]) == "-h") {
                std::cout << "HELP" << std::endl;
            } else {
                std::cout << argv[fc] << " is an invalid flag." << std::endl;
            }
        }
    }

    receiver = new PacketReceiver();
    analyzer = new PacketAnalyzer();

    /* TESTING READING PACKETS FROM DATA FILE: */
//    receiver->openInputFile("oneMinuteCapture.dat");
//    // load k number of packets from the data file
//    for (int k = 0; k < 10000; ++k) {
//        //receiver->inputFile.seekg(DATABUFLEN, std::ios::cur);   // move the current position in the ifstream to the next packet
//        //receiver->readSingleDataPacketFromFile();
//        receiver->readDataPacketsFromFile(1);
//        if (receiver->getPacketQueueSize() > 0) {
//            analyzer->loadPacket(receiver->getNextQueuedPacket());
//            receiver->popQueuedPacket(); // this packet will be analyzed, get it out of the queue
//            std::vector<CartesianPoint> newPoints(analyzer->getCartesianPoints());
//            std::cout << "Number of points: " << newPoints.size() << std::endl;
//            for (unsigned j = 0; j < newPoints.size(); ++j) {
//                queue.enqueue(newPoints[j]);
//            }
//        }
//     }

    /* END TESTING */

    /* Initialize components based on the flags: */
    initKernel();

    if (graphicsModeEnabled) {
        if (initGraphics() == 1) {
            return 1;
        }
    }

    if (streamModeEnabled) {
        initPacketHandling(dataFileName);
    }

    /* Begin the main loop on this thread: */
    mainLoop();

    if (streamModeEnabled) {
        stopPacketHandling();
    }

    /* Memory Cleanup: */
    delete analyzer;
    delete receiver;

    return 0;
}

void mainLoop() {
    previousTime = SDL_GetTicks();
    bool loop = true;
    while (loop) {
        CartesianPoint p;
        while (queue.try_dequeue(p)) {
            grid.addPoint(p);
        }

        if (graphicsModeEnabled) {
            /**************************** HANDLE CONTROLS ********************************/
            int timeToQuit = handleControls(); // returns non-zero if quit events happen
            if (timeToQuit) {
                loop = false;
            }

            /**************************** DO THE DRAWING *********************************/
            // draw the tree
//        treeDrawer.drawQuadTree(quadTree, (float) zoomLevel);
            // draw the grid
            gridDrawer.drawGrid();
            // update the screen
            graphics.render();
        }
    }
}

// This function runs inside the listeningThread.
int listeningThreadFunction(void* arg) {
    std::cout << "\nPacket handling thread is active.\n";
    while (!packetHandlerQuit) {
        /*************************** HANDLE PACKETS *********************************/
        if (streamMedium == Velodyne) {
            receiver->listenForDataPacket(); // make sure to take the lack of UDP header into account
            if (receiver->getPacketQueueSize() > 0) {
                if (writeModeEnabled) {
                    receiver->writePacketToFile(receiver->getNextQueuedPacket());
                }
                analyzer->loadPacket(receiver->getNextQueuedPacket());
                receiver->popQueuedPacket();    // packet has been read, get rid of it
                std::vector<CartesianPoint> newPoints(analyzer->getCartesianPoints());
                for (unsigned j = 0; j < newPoints.size(); ++j) {
                    queue.enqueue(newPoints[j]);
                }
            }
        } else if (streamMedium == file) {
//            receiver->openInputFile("oneMinuteCapture.dat");
//            // load k number of packets from the data file
//            for (int k = 0; k < 10000; ++k) {
//                //receiver->inputFile.seekg(DATABUFLEN, std::ios::cur);   // move the current position in the ifstream to the next packet
//                //receiver->readSingleDataPacketFromFile();
//                receiver->readDataPacketsFromFile(1);
//                if (receiver->getPacketQueueSize() > 0) {
//                    analyzer->loadPacket(receiver->getNextQueuedPacket());
//                    receiver->popQueuedPacket(); // this packet will be analyzed, get it out of the queue
//                    std::vector<CartesianPoint> newPoints(analyzer->getCartesianPoints());
//                    std::cout << "Number of points: " << newPoints.size() << std::endl;
//                    for (unsigned j = 0; j < newPoints.size(); ++j) {
//                        queue.enqueue(newPoints[j]);
//                    }
//                }
//             }
        }
    }
    std::cout << "Packet handling thread is dying.\n";
    return 0;
}

int handleControls() {
    /**************************** HANDLE EVENTS *********************************/
    SDL_Event event;
    while (SDL_PollEvent(&event)) { // process all accumulated events
        if (event.type == SDL_QUIT) { // quit events (like if you press the x)
            return 1;
        } else if (event.type == SDL_KEYDOWN) { // keydown events (generated only once per key press)
            switch (event.key.keysym.sym) {
                case SDLK_ESCAPE:
                    return 1;
                default:
                    break;
            }
        } else if (event.type == SDL_MOUSEBUTTONDOWN) {
            if (event.button.button == SDL_BUTTON_LEFT) {   // add point to grid
                float xPos = (float)event.button.x / (graphics.getResX() * 0.5f) - 1.0f;
                float yPos = -(float)event.button.y / (graphics.getResY() * 0.5f) + 1.0f;
                glm::dmat4 invMat = glm::inverse(gridDrawer.getTransformMat());
                glm::dvec4 scrSpaceClick(xPos, yPos, 0.0, 1.0);
                glm::dvec4 gridSpaceClick = invMat * scrSpaceClick;
                grid.addPoint({(float) gridSpaceClick.x, (float) gridSpaceClick.y, 0.f});
            }
            else if (event.button.button == SDL_BUTTON_RIGHT) {   // add point to grid
                float xPos = (float)event.button.x / (graphics.getResX() * 0.5f) - 1.0f;
                float yPos = -(float)event.button.y / (graphics.getResY() * 0.5f) + 1.0f;
                glm::dmat4 invMat = glm::inverse(gridDrawer.getTransformMat());
                glm::dvec4 scrSpaceClick(xPos, yPos, 0.0, 1.0);
                glm::dvec4 gridSpaceClick = invMat * scrSpaceClick;
                std::cout << grid.getKernelOutput((float)gridSpaceClick.x, (float)gridSpaceClick.y) << std::endl;
            }
        } else if (event.type == SDL_MOUSEWHEEL) {
            int xPosInt, yPosInt;
            SDL_GetMouseState(&xPosInt, &yPosInt);
            float xPos = (float)xPosInt / (graphics.getResX() * 0.5f) - 1.0f;
            float yPos = -(float)yPosInt / (graphics.getResY() * 0.5f) + 1.0f;
            glm::dvec4 scrSpacePos(xPos, yPos, 0.0, 1.0);
            float zoomSpeed = 1.f + event.wheel.y * SCROLL_ZOOM_SENSITIVITY;
            zoomLevel /= zoomSpeed;
            gridDrawer.zoomAtPoint((float)scrSpacePos.x, (float)scrSpacePos.y, zoomSpeed);
        }
    }
    /**************************** HANDLE KEY STATE *********************************/
    // Determine the time step since last time
    currentTime = SDL_GetTicks();
    deltaTime = currentTime - previousTime;
    previousTime = currentTime;
    // Get current keyboard state ( this is used for smooth controls rather than key press event controls above )
    const Uint8* keyStates = SDL_GetKeyboardState(NULL);
    float moveSpeed = KEY_MOVE_SENSITIVITY * deltaTime;
    // apply movement according to which keys are down
    if (keyStates[SDL_SCANCODE_UP]) {
        gridDrawer.translate(0.0f, -moveSpeed * gridDrawer.getMovementScaleY() * (float) zoomLevel);
    }
    if (keyStates[SDL_SCANCODE_DOWN]) {
        gridDrawer.translate(0.0f, moveSpeed * gridDrawer.getMovementScaleY() * (float) zoomLevel);
    }
    if (keyStates[SDL_SCANCODE_LEFT]) {
        gridDrawer.translate(moveSpeed * gridDrawer.getMovementScaleX() * (float) zoomLevel, 0.0f);
    }
    if (keyStates[SDL_SCANCODE_RIGHT]) {
        gridDrawer.translate(-moveSpeed * gridDrawer.getMovementScaleX() * (float) zoomLevel, 0.0f);
    }
    if (keyStates[SDL_SCANCODE_LSHIFT]) {
        float zoomSpeed = 1.f + moveSpeed * KEY_ZOOM_EXTRA_SENSITIVITY;
        zoomLevel /= zoomSpeed;
        gridDrawer.scale(zoomSpeed, zoomSpeed);
    }
    if (keyStates[SDL_SCANCODE_LCTRL]) {
        float zoomSpeed = 1.f - moveSpeed * KEY_ZOOM_EXTRA_SENSITIVITY;
        zoomLevel /= zoomSpeed;
        gridDrawer.scale(zoomSpeed, zoomSpeed);
    }
    return 0;
}

void initPacketHandling(std::string filename) {
    // packet handler setup
    receiver->openOutputFile(filename);
    receiver->bindSocket();

    // spawn the listening thread, passing it information in "data"
    packetHandlerQuit = false;
    packetListeningThread = SDL_CreateThread (listeningThreadFunction, "listening thread", NULL);
}

void stopPacketHandling() {
    // once the main loop has exited, set the listening thread's "quit" to true and wait for the thread to die.
    packetHandlerQuit = true;
    SDL_WaitThread(packetListeningThread, NULL);
}

void initKernel() {
    /* KERNEL IMPLEMENTATIONS: */
    auto avgKernel = [] (Grid<CartesianPoint>* pGrid, int x, int y){ // x and y determine which grid cell is being operated on
        int totalPointsCounted = 0;
        float average = 0;
        int gridIndex = y * pGrid->getXRes() + x; // index into the vector of GridCells
        for (unsigned i = 0; i < pGrid->kernel.contributingCells.size(); ++i) {
            if (x + pGrid->kernel.contributingCells[i].xOffset >= 0 && x + pGrid->kernel.contributingCells[i].xOffset < pGrid->getXRes()) {
                int gridIndexOffset = pGrid->kernel.contributingCells[i].yOffset * pGrid->getXRes(); // offset from the gridIndex to the current contributing cell
                gridIndexOffset += pGrid->kernel.contributingCells[i].xOffset;
                int ccIndex = gridIndex + gridIndexOffset; // contributingCellIndex
                if (ccIndex >= 0 && ccIndex < pGrid->cells.size()) {
                    for (unsigned j = 0; j < pGrid->cells[ccIndex].points.size(); ++j) {
                        average += pGrid->cells[ccIndex].points[j].y;
                    }
                    totalPointsCounted += pGrid->cells[ccIndex].points.size();
                }
            }
        }
        if (totalPointsCounted > 0) {
            average /= totalPointsCounted;
        }
        pGrid->cells[gridIndex].kernelOutput = average;
    };

    avgKernel(&grid, 0, 0);

    int kernelError = grid.specifyKernel(
            {
                    0, 1, 0,
                    1, 1, 1,
                    0, 1, 0,
            }, avgKernel);
    if (kernelError) {
        std::cout << "KERNEL FAILURE!\n";
    }
}

int initGraphics() {
    zoomLevel = 0.01f;
    std::stringstream log;
    if (!graphics.init(log)) { // if init fails, exit
        std::cout << log.str();
        return 1;
    }
    std::cout << log.str();
    std::cout << gridDrawer.init(graphics.getAspectRatio(), &grid);

    #ifdef BENCHMARK_QUADTREE_POINT_INSERTION
        // Benchmark point insertion
        quadTree.setMaxPointsPerNode(50);
        float invPi = 1.f / 3.14159265358979f;
        float startTime = SDL_GetTicks();
        for (unsigned i = 0; i < 1000000; ++i) {
            quadTree.addPoint({(float)i * (float)sin(i * invPi) * 0.1f, (float)i * (float)cos(i * invPi) * 0.1f});
        }
        std::cout << SDL_GetTicks() - startTime;
    #endif
    return 0;
}

void enableGraphicsMode() {
    graphicsModeEnabled = true;
    std::cout << "Graphical display ENABLED." << std::endl;
}

void disableGraphicsMode() {
    graphicsModeEnabled = false;
    std::cout << "Graphical display DISABLED" << std::endl;
}

void enableStreamMode() {
    streamModeEnabled = true;
    std::cout << "Data streaming ENABLED." << std::endl;
    getStreamDevice();
}

void disableStreamMode() {
    streamModeEnabled = false;
    std::cout << "Data streaming DISABLED." << std::endl;
}

void getStreamDevice() {
    std::cout << "Select the device you are streaming data from (enter the associated number):" << std::endl;
    std::cout << "1. Velodyne" << std::endl;
    std::cout << "2. File" << std::endl;
    std::string input;
    std::getline(std::cin, input);
    int inputInt = -1;
    while (inputInt == -1) {
        inputInt = std::stoi(input);
        switch (inputInt) {
            case (1):
                streamMedium = Velodyne;
                std::cout << "Selected streaming medium: VELODYNE." << std::endl;
                break;
            case (2):
                streamMedium = file;
                std::cout << "Selected streaming medium: FILE." << std::endl;
                break;
            default:
                std::cout << "Invalid input. Please select a valid number." << std::endl;
                inputInt = -1;
                break;
        }
    }
}

void enableWriteMode() {
    writeModeEnabled = true;
    std::cout << "Data writing ENABLED." << std::endl;
    std::cout << "What would you like to name the output data file? ";
    std::getline(std::cin, dataFileName);
    dataFileName.append(".dat");
    std::cout << "Data will be written to: " << dataFileName << std::endl;
}

void disableWriteMode() {
    writeModeEnabled = false;
    std::cout << "Data writing DISABLED." << std::endl;
}