[![Build Status](https://dev.azure.com/git/git/_apis/build/status/git.git)](https://dev.azure.com/git/git/_build/latest?definitionId=11)


Git - fast, scalable, distributed revision control system
=========================================================
This is a clone of the official Git repository <https://github.com/git/git>, which has been modified to integrate and test the Almost RERERE approach.

Git is a fast, scalable, distributed revision control system with an
unusually rich command set that provides both high-level operations
and full access to internals.

Git is an Open Source project covered by the GNU General Public
License version 2 (some parts of it are under different licenses,
compatible with the GPLv2). It was originally written by Linus
Torvalds with help of a group of hackers around the net.

Many Git online resources are accessible from <https://git-scm.com/>
including full documentation and Git related tools.


 Git - Almost RERERE
 =========================================================
 Almost RERERE extends the capabilities of Git RERERE. Git code has been modified, the compilation and installation process of Git requires changes explained in the following.

>:warning: **Warning: Following this procedure will replace your current installation of Git, to return to the official version of Git you will have to reinstall it from the official sources**

## Configuration and Compilation instructions:
- **Step 1**: Almost-RERERE uses json-c, you need to clone the repository, compile and install the library. Follow the instructions on <https://github.com/json-c/json-c>.

  After installing the library, depending on your distro and system configuration you might need to reboot the system for the changes to take effect, or execute a command to reload the libraries. In ubuntu/debian distros you can execute:
  ```
  >sudo ldconfig
   ```

- **Step 2**: Git uses a 'makefile' to compile the code, You will need to modify the file depending on your development environment, as follow:
   
   **_For macOS_**, you will need to modify 1 block - line 1187:
   ```
   JSON_CFLAGS += $(shell pkg-config --cflags json-c)
   JSON_LDFLAGS += $(shell pkg-config --libs json-c)
   ALL_CFLAGS = $(DEVELOPER_CFLAGS) $(CPPFLAGS) $(CFLAGS) $(JSON_CFLAGS)
   ALL_LDFLAGS = $(LDFLAGS) $(JSON_LDFLAGS)
   ```   
   **_For linux(Ubuntu)_**, modifications are required in 2 blocks
   
   First - line 1188:
    ```   
    JSON_C_DIR=/usr/local
    CFLAGS += -I$(JSON_C_DIR)/include/json-c
    LDFLAGS+= -L$(JSON_C_DIR)/lib -ljson-c
    
    ALL_CFLAGS = $(DEVELOPER_CFLAGS) $(CPPFLAGS) $(CFLAGS) $(JSON_CFLAGS)
    ALL_LDFLAGS = $(LDFLAGS) $(JSON_LDFLAGS)
    ```
   
   Second - line 1977:
   ```   
   LIBS = $(filter-out %.o, $(GITLIBS)) $(EXTLIBS)  -ljson-c
   ``` 

   **_For Windows_**, It depends on the choice of compiler, version of Make tool and IDE. Most of the tools follow linux syntax, check the documentation of your development environment.

- **Step 4**: Copy the content of the rerere-lib folder to a location in your system that is available for Git to use, and give read and execution permissions. The current location is:
   ```
   /almost-rerere/
   ```

- **Step 5**: If you select a different location that the above mention, you will have to update the path on the rerere.c file. Modify path as indicated in the following:

   Line  938:

   &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;execl("/usr/bin/java", "/usr/bin/java", "-jar", "`/almost-rerere/`RegexReplacement.jar","`/almost-rerere/`",groupId,conflict,(char*)0);

   Line 2118:

   &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;id_array[2] = "`/almost-rerere/`RandomSearchReplaceTurtle.jar";

   If you put the files in the above location you can omit this step.
- **Step 6**: Almost RERERE uses the zlib library, some distributions on linux include it by default, verify that the library is present on the `/usr/include` folder before trying to compile the script. In case the library is not present, install it before going forward. In ubuntu/debian distros it can be installed with the following command:
   ```
   > sudo apt-get install libz-dev
   ```
- **Step 7**: You can now compile Git following the instructions provided in the file [INSTALL](https://github.com/herrera-sergio/AlmostRERERE/blob/main/Git/INSTALL). In most cases, you will need only to execute the following commands:
   ```
   >make
   >make install
   ```
   But please refer to the installation documentation before proceeding with this step, since your current Git installation will be overwritten.

## Usage instructions:
You need to setup Almost RERERE to start using it with your git project.
- **Step 7**: You need to enable Git RERERE, this can be done at project level by creating the *'rr-cache'* folder under the *'.git'* folder of the project. It can be done executing the mkdir command inside the folder:
   ```
   ~/your_project/.git> mkdir rr-cache 
   ```

- **Step 8**: Adjust the paths on the 'configuration.properties' file (on the location specified in Step 5) to the absolute path of *your project*. For example, the current path for the conflict_file property looks as follows:

   &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;conflict_file=`/home/almost-rerere/almost-test/`.git/rr-cache/conflict_index.json

   It should be changed to:

   &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;conflict_file=`/path/of/your/project/`.git/rr-cache/conflict_index.json

   The path of the 4 properties in the file should be changed.

- **Step 9**: Almost RERERE will work automatically on background when certain Git commands are executed(merge, am, rebase, commit), when a conflict emerges and a resolution can be computed the tool will display the proposed solution on the git console.