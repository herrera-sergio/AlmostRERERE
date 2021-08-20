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

## Configuration and configuration instructions:
- **Step 1**: Almost-RERERE uses json-c, you need to clone the repository, compile and install the library. Follow the instructions on <https://github.com/json-c/json-c>

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

- **Step 4**:


 - Compile git project
 - Enable Rerere : create .git/rr-cache/ directory
 - Prepare additional java components:
    - clone <https://github.com/manantariq/Search-and-Replace>
    - Update the path in config.properties
    - Update the path in rerere.c for RandomSearchReplaceTurtle.jar and RegexReplacement.jar

[INSTALL]: INSTALL
[Documentation/gittutorial.txt]: Documentation/gittutorial.txt
[Documentation/giteveryday.txt]: Documentation/giteveryday.txt
[Documentation/gitcvs-migration.txt]: Documentation/gitcvs-migration.txt
[Documentation/SubmittingPatches]: Documentation/SubmittingPatches
Please read the file [INSTALL][] for installation instructions.