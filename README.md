# TaskRunner

A concurrent task execution framework with support for task queues, workers, and barriers.

## Features

- **Task Queues**: Manage and execute tasks in separate queues.
- **Workers**: Execute tasks from assigned queues.
- **Barriers**: Synchronize multiple task queues based on arrival and waiting conditions.
- **Scheduler**: Assign tasks to workers and manage their execution.

## Getting Started

### Prerequisites

- CMake 3.10+
- Git

### Installation

1. Clone the repository:

    ```sh
    git clone https://github.com/yourusername/TaskRunner.git
    cd TaskRunner
    ```

2. Build the project:

    ```sh
    mkdir build
    cd build
    cmake ..
    make
    ```

3. Run the application:

    ```sh
    ./TaskRunner
    ```

4. Run tests:

    ```sh
    ctest
    ```

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
