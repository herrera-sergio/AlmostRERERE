#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <json-c/json.h>
#include <zconf.h>
#include <sys/wait.h>
#include <stdlib.h>  // rand(), srand()
#include <time.h>    // time()
#include <errno.h>

#define SCALING_FACTOR 0.1
//#define similarity_th 0.80
#define intrasimilarity_th 0.90
#define valid_cluster_th 0.77

#define CONFIG_FILE_PATH "config.properties"

#define CONFLICT_INDEX_FILENAME ".git/rr-cache/conflict_index.json"
#define CONFLICT_INDEX_RECLUSTER_FILENAME ".git/rr-cache/conflict_index_recluster.json"
#define STRING_REPLACE_FILENAME ".git/rr-cache/string_replace.txt"
#define STRING_REPLACE_RESULT_FILENAME  ".git/rr-cache/regex_replace_result.txt"
#define PERFORMANCE_FILENAME ".git/rr-cache/performance.txt"
#define CLUSTER_STATISTICS_FILENAME ".git/rr-cache/statistics.txt"

#define REGEX_REPLACEMENT_JAR "RegexReplacement.jar"
#define RANDOM_SEARCH_REPLACE_JAR "RandomSearchReplaceTurtle.jar"

#define CONFLICT_INDEX 0
#define CONFLICT_INDEX_RECLUSTER 1
#define STRING_REPLACE 2
#define STRING_REPLACE_RESULT 3
#define PERFORMANCE 4
#define CLUSTER_STATISTICS 5

double similarity_th=0.80;
char *groupId_list = NULL;
int cluster_population = 0;

char *file_names[5];

//loaded from config.properties
char *workdir_path = NULL;

int conf_ID=0;


static char *build_filename(const char *filename) {
    char *result = malloc(strlen(workdir_path) + strlen(filename) + 1);
    strcpy(result, workdir_path);
    strcat(result, filename);
    return result;
}

static char *concat(const char *str1, const char *str2) {
    char *result = malloc(strlen(str1) + strlen(str2) + 1);
    strcpy(result, str1);
    strcat(result, str2);
    return result;
}


static char *escapeCSV(char *in) {
    int in_len = strlen(in);
    char *out_buf = malloc(in_len * 2 + 3);
    int out_idx = 0;
    int in_idx = 0;

    for (in_idx = 0; in_idx < in_len; in_idx++) {
        if (in[in_idx] == '"') {
            out_buf[out_idx++] = '\"';
            out_buf[out_idx++] = '\"';
        } else {
            out_buf[out_idx++] = in[in_idx];
        }
    }
    out_buf[out_idx++] = 0;
    return out_buf;
}

static int max(int x, int y) {
    return x > y ? x : y;
}

static int min(int x, int y) {
    return x < y ? x : y;
}

static int starts_with(const char *a, const char *b){
    if(strncmp(a, b, strlen(b)) == 0) return 1;
    return 0;
}

static char* apply_mask(const char *s){

    if (strstr(s, "import java.") != NULL && starts_with(s, "import java.")==1) {
        char *s_masked=malloc(strlen(s)-12 + 1);
        memcpy( s_masked, &s[12], strlen(s)-12 );
        s_masked[strlen(s)-12]='\0';
        return s_masked;
    }
    else if (strstr(s, "import javax.") != NULL && starts_with(s, "import javax.")==1) {
        char *s_masked=malloc(strlen(s)-13 + 1);
        memcpy( s_masked, &s[13], strlen(s)-13 );
        s_masked[strlen(s)-13]='\0';
        return s_masked;
    }
    else if(strstr(s, "import org.atlasapi.") != NULL && starts_with(s, "import org.atlasapi.")==1){//atlas
        char *s_masked=malloc(strlen(s)-20 + 1);
        memcpy( s_masked, &s[20], strlen(s)-20 );
        s_masked[strlen(s)-20]='\0';
        return s_masked;
    }
    else if(strstr(s, "import org.elasticsearch.index.") != NULL && starts_with(s, "import org.elasticsearch.index.")==1)  {//slastic
        char *s_masked=malloc(strlen(s)-31 + 1);
        memcpy( s_masked, &s[31], strlen(s)-31 );
        s_masked[strlen(s)-31]='\0';
        return s_masked;
    }
    else if(strstr(s, "import org.elasticsearch.common.") != NULL && starts_with(s, "import org.elasticsearch.common.")==1)  {
        char *s_masked=malloc(strlen(s)-32 + 1);
        memcpy( s_masked, &s[32], strlen(s)-32 );
        s_masked[strlen(s)-32]='\0';
        return s_masked;
    }
    else if(strstr(s, "import org.elasticsearch.") != NULL && starts_with(s, "import org.elasticsearch.")==1)  {
        char *s_masked=malloc(strlen(s)-25 + 1);
        memcpy( s_masked, &s[25], strlen(s)-25 );
        s_masked[strlen(s)-25]='\0';
        return s_masked;
    }
    else if(strstr(s, "import freenet.") != NULL && starts_with(s, "import freenet.")==1) {//fred
	char *s_masked=malloc(strlen(s)-15 + 1);
        memcpy( s_masked, &s[15], strlen(s)-15 );
        s_masked[strlen(s)-15]='\0';
        return s_masked;
    }
    else if(strstr(s, "import io.realm.") != NULL && starts_with(s, "import io.realm.")==1) {//realm
        char *s_masked=malloc(strlen(s)-16 + 1);
        memcpy( s_masked, &s[16], strlen(s)-16 );
        s_masked[strlen(s)-16]='\0';
        return s_masked;
    }
    else if(strstr(s, "import org.antlr.v4.") != NULL && starts_with(s, "import org.antlr.v4.")==1){//antler
	char *s_masked=malloc(strlen(s)-20 + 1);
        memcpy( s_masked, &s[20], strlen(s)-20 );
        s_masked[strlen(s)-20]='\0';
        return s_masked;
    }
    else if(strstr(s, "import ro.isdc.wro.") != NULL && starts_with(s, "import ro.isdc.wro.")==1){//wro4j
        char *s_masked=malloc(strlen(s)-19 + 1);
        memcpy( s_masked, &s[19], strlen(s)-19 );
        s_masked[strlen(s)-19]='\0';
        return s_masked;
    }
    else if(strstr(s, "import org.zkoss.") != NULL && starts_with(s, "import org.zkoss.")==1){//zk
        char *s_masked=malloc(strlen(s)-17 + 1);
        memcpy( s_masked, &s[17], strlen(s)-17 );
        s_masked[strlen(s)-17]='\0';
        return s_masked;
    }
    else if(strstr(s, "import com.eucalyptus.") != NULL && starts_with(s, "import com.eucalyptus.")==1){//eucalyptus
        char *s_masked=malloc(strlen(s)-22 + 1);
        memcpy( s_masked, &s[22], strlen(s)-22 );
        s_masked[strlen(s)-22]='\0';
        return s_masked;
    }
    else if(strstr(s, "import org.nuxeo.ecm.") != NULL && starts_with(s, "import org.nuxeo.ecm.")==1){//nuxeo
        char *s_masked=malloc(strlen(s)-21 + 1);
        memcpy( s_masked, &s[21], strlen(s)-21 );
        s_masked[strlen(s)-21]='\0';
        return s_masked;
    }
    else if(strstr(s, "import org.geometerplus.") != NULL && starts_with(s, "import org.geometerplus.")==1){//android
        char *s_masked=malloc(strlen(s)-24 + 1);
        memcpy( s_masked, &s[24], strlen(s)-24 );
        s_masked[strlen(s)-24]='\0';
        return s_masked;
    }
    else if(strstr(s, "import org.keycloak.") != NULL && starts_with(s, "import org.keycloak.")==1){//keycloak
        char *s_masked=malloc(strlen(s)-20 + 1);
        memcpy( s_masked, &s[20], strlen(s)-20 );
        s_masked[strlen(s)-20]='\0';
        return s_masked;
    }
    else if(strstr(s, "import org.zanata.") != NULL && starts_with(s, "import org.zanata.")==1){//zanata
        char *s_masked=malloc(strlen(s)-18 + 1);
        memcpy( s_masked, &s[18], strlen(s)-18 );
        s_masked[strlen(s)-18]='\0';
        return s_masked;
    }
    else if (strstr(s, "import org.") != NULL && starts_with(s, "import org.")==1) {
        char *s_masked=malloc(strlen(s)-11 + 1);
        memcpy( s_masked, &s[11], strlen(s)-11 );
        s_masked[strlen(s)-11]='\0';
        return s_masked;
    }
    else if (strstr(s, "import com.") != NULL && starts_with(s, "import com.")==1) {
        char *s_masked=malloc(strlen(s)-11 + 1);
        memcpy( s_masked, &s[11], strlen(s)-11 );
        s_masked[strlen(s)-11]='\0';
        return s_masked;
    }
    else if (strstr(s, "import net.") != NULL && starts_with(s, "import net.")==1) {
        char *s_masked=malloc(strlen(s)-11 + 1);
        memcpy( s_masked, &s[11], strlen(s)-11 );
        s_masked[strlen(s)-11]='\0';
        return s_masked;
    }
    else if(strstr(s, "import edu.") != NULL && starts_with(s, "import edu.")==1){
    	char *s_masked=malloc(strlen(s)-11 + 1);
        memcpy( s_masked, &s[11], strlen(s)-11 );
        s_masked[strlen(s)-11]='\0';
        return s_masked;
    }
    else {
        return (char *)s;
    }
}

static double jaro_winkler_distance(const char *s, const char *a) {
    //char *s=apply_mask(s_unmask);
    //char *a=apply_mask(a_unmask);

    int i, j, l;
    int m = 0, t = 0;
    int sl = strlen(s);
    int al = strlen(a);
    int sflags[sl], aflags[al];
    int range = max(0, max(sl, al) / 2 - 1);
    double dw;

    if (!sl || !al) {
        return 0.0;
    }

    for (i = 0; i < al; i++)
        aflags[i] = 0;

    for (i = 0; i < sl; i++)
        sflags[i] = 0;

    /* calculate matching characters */
    for (i = 0; i < al; i++) {
        for (j = max(i - range, 0), l = min(i + range + 1, sl); j < l; j++) {
            if (a[i] == s[j] && !sflags[j]) {
                sflags[j] = 1;
                aflags[i] = 1;
                m++;
                break;
            }
        }
    }

    if (!m)
        return 0.0;

    /* calculate character transpositions */
    l = 0;
    for (i = 0; i < al; i++) {
        if (aflags[i] == 1) {
            for (j = l; j < sl; j++) {
                if (sflags[j] == 1) {
                    l = j + 1;
                    break;
                }
            }
            if (a[i] != s[j])
                t++;
        }
    }
    t /= 2;

    /* Jaro distance */
    dw = (((double) m / sl) + ((double) m / al) + ((double) (m - t) / m)) / 3.0;

    if(dw<=0.7){ //added based on Python implementation
        return dw;
    }

    /* calculate common string prefix up to 4 chars */
    l = 0;
    for (i = 0; i < min(min(sl, al), 4); i++)
        if (s[i] == a[i])
            l++;

    /* Jaro-Winkler distance */
    dw = dw + (l * SCALING_FACTOR * (1 - dw));

    //fprintf_ln(stderr, _("jaroW: %lf\n"),dw);
    return dw;
}

static void executeRegexJar(const char *group_id, int recluster, size_t cluster_size) {
    time_t start;
    time_t end;
    FILE *performance_log = fopen(file_names[PERFORMANCE], "a+");

    if (group_id) {

        int length = 5 + 1; //groupId_list.nr know only at runtime
        const char **id_array = malloc(sizeof(*id_array) * length);

        id_array[0] = "/usr/bin/java"; //TODO make configurable javagent
        id_array[1] = "-jar";
        id_array[2] = RANDOM_SEARCH_REPLACE_JAR;
        if (recluster == 0) { //TODO check
            id_array[3] = "./"; //config.properties path
        } else {
            id_array[3] = "./";
        }

        id_array[4] = group_id;
        id_array[length - 1] = NULL; //terminator need for execv
        printf("JAVA COMMAND:%s \n", id_array[4]);

        //TODO adjust the path to jar file
        time(&start);
        pid_t pid = fork();
        if (pid == 0) { // child process
            /* open /dev/null for writing */
            //printf("\n\n\nJAR PROCESS IS STARTING IN BACKGROUND!!!!!\n\n\n");
            int fd = open("/dev/null", O_WRONLY);
            dup2(fd, 1);    /* make stdout a copy of fd (> /dev/null) */
            close(fd);
            execv("/usr/bin/java", (void *) id_array);
            time(&start);
        } else { //parent process
            int status;
            //sleep(1);
            printf("Parent is waiting for the Recluster.........");
            if (waitpid(pid, &status, 0) > 0) {

                if (WIFEXITED(status) && !WEXITSTATUS(status)) {
                    time(&end);
                    printf("Program execution successful!!!! in %.f secs \n", difftime(end, start));
                    fprintf(performance_log, "\"%s\",\"%zu\",\"%.f\"\n", group_id,
                            cluster_size, difftime(end, start));

                } else if (WIFEXITED(status) && WEXITSTATUS(status)) {
                    if (WEXITSTATUS(status) == 127) {
                        // execv failed
                        printf("execv failed\n");
                    } else {
                        time(&end);
                        printf("Program terminated normally,"
                               " but returned a non-zero status:%f \n", difftime(end, start));
                        exit(-666);
                    }
                } else {
                    printf("program didn't terminate normally\n");
                }
            } else {
                // waitpid() failed
                printf("waitpid() failed\n");
            }

        }
        free(id_array);
        fclose(performance_log);
    }
}

static double cluster_cluster_similarity(const struct json_object *val, const struct json_object *val2) {
    double total_similarity = 0;
    double jaroW_conf = 0;
    double jaroW_resol = 0;
    struct json_object *obj;
    struct json_object *obj2;
    const char *jconf1;
    const char *jresol1;
    const char *jconf2;
    const char *jresol2;
    int arraylen = json_object_array_length(val);
    int arraylen2 = json_object_array_length(val2);
    int comparisons = 0;
    double total_comparison = 0;
    for (int i = 0; i < arraylen; i++) {
        obj = json_object_array_get_idx(val, i);
        jconf1 = json_object_get_string(json_object_object_get(obj, "conflict"));
        jresol1 = json_object_get_string(json_object_object_get(obj, "resolution"));
        if (jconf1 == NULL || jresol1 == NULL) {
            printf("Something is wrong..........................\n");
        }

        double subcluster_similarity = 0;
        for (int j = 0; j < arraylen2; j++) {
            obj2 = json_object_array_get_idx(val2, j);
            jconf2 = json_object_get_string(json_object_object_get(obj2, "conflict"));
            jresol2 = json_object_get_string(json_object_object_get(obj2, "resolution"));
            if (jconf2 == NULL || jresol2 == NULL) {
                printf("Something is wrong2..........................\n");
            }
            jaroW_conf = jaro_winkler_distance(jconf1, jconf2);
            jaroW_resol = jaro_winkler_distance(jresol1, jresol2);
            double sim = (jaroW_conf + jaroW_resol) / 2;
            /*Varibles for the ponderated solution, evaluates higher than the direct solution*/
            subcluster_similarity += sim;
            /*variables for direct solution, evaluates equal to excel*/
            total_comparison += sim;
            comparisons++;
        }
        total_similarity += subcluster_similarity / (arraylen - (i + 1));

    }

    //printf("%f, %f\n",total_similarity/(arraylen-1),(total_comparison/comparisons));
    //return (total_similarity/(arraylen-1));
    return (total_comparison / comparisons);
}

static const char *get_conflict_json_id_empty(char *conflict, char *resolution) {
    struct json_object *file_json = json_object_from_file(file_names[CONFLICT_INDEX]);
    if (!file_json) { // if file is empty
        if (!resolution) { //resolution is NULL
            return NULL;
        }
        return "1";
    }

    double jaroW = 0;

    const char *groupId = NULL;
    double max_sim = similarity_th;
    double local_max_sim = 0;
    int idCount = 0;

    struct json_object *obj;
    const char *jconf;
    const char *jresol;
    int arraylen;

    double total_similarity = 0;
    json_object_object_foreach(file_json, key, val) {
        obj = NULL;
        arraylen = json_object_array_length(val);
        idCount += 1;
        total_similarity = 0;
        for (int i = 0; i < arraylen; i++) {
            obj = json_object_array_get_idx(val, i);
            jconf = json_object_get_string(json_object_object_get(obj, "conflict"));
            jresol = json_object_get_string(json_object_object_get(obj, "resolution"));

            if (strcmp(jconf, "") == 0 && strcmp(jresol, "") == 0) {
                jaroW = 1;
            } else {
                jaroW = 0;
            }
            total_similarity += jaroW;
        }
        double avg = total_similarity / arraylen;
        if (avg >= local_max_sim) {
            local_max_sim = avg;
        }

        if (avg >= max_sim) {
            max_sim = avg;
            groupId = key;
        }
    }
    printf("Regular Local max Similarity: %f\n", local_max_sim);
    printf("Regular MAX Similarity: %f\n", max_sim);

    char *id = malloc(sizeof(char *));

    if (!groupId && resolution) { //if group == null and resolution != null
        //create new group id
        json_object_put(file_json);
        id = (char *) json_object_to_json_string(json_object_new_int(idCount + 1));
    } else if (groupId) {
        memcpy(id, groupId, strlen(groupId) + 1);
        json_object_put(file_json);
    } else {
        groupId = "0";
        memcpy(id, groupId, strlen(groupId) + 1);
        json_object_put(file_json);
    }
    return id;

}

static const char *get_conflict_json_id_empty_conf(char *conflict, char *resolution) {
    struct json_object *file_json = json_object_from_file(file_names[CONFLICT_INDEX]);
    if (!file_json) { // if file is empty
        if (!resolution) { //resolution is NULL
            return NULL;
        }
        return "1";
    }

    double jaroW = 0;
    //size_t leve = 0 ;
    const char *groupId = NULL;
    double max_sim = similarity_th;
    double local_max_sim = 0;
    int idCount = 0;

    struct json_object *obj;
    const char *jconf;
    const char *jresol;
    int arraylen;

    double total_similarity = 0;
    json_object_object_foreach(file_json, key, val) {
        obj = NULL;

        arraylen = json_object_array_length(val);
        idCount += 1;
        total_similarity = 0;
        for (int i = 0; i < arraylen; i++) {
            obj = json_object_array_get_idx(val, i);
            jconf = json_object_get_string(json_object_object_get(obj, "conflict"));
            jresol = json_object_get_string(json_object_object_get(obj, "resolution"));

            if (strcmp(jconf, "") == 0) {
                //TODO check if necessary
                jaroW = jaro_winkler_distance(conflict, jresol);
            } else {
                jaroW = 0;
            }
            total_similarity += jaroW;
        }
        double avg = total_similarity / arraylen;
        if (avg >= local_max_sim) {
            local_max_sim = avg;
        }

        if (avg >= max_sim) {
            max_sim = avg;
            groupId = key;
        }
    }
    printf("Regular Local max Similatirt: %f\n", local_max_sim);
    printf("Regular MAX Similarity: %f\n", max_sim);

    char *id = malloc(sizeof(char *));
    //json_object_put(file_json);
    if (!groupId && resolution) { //if group == null and resolution != null
        //create new group id
        json_object_put(file_json);
        id = (char *) json_object_to_json_string(json_object_new_int(idCount + 1));
    } else if (groupId) { //groupid != null
        memcpy(id, groupId, strlen(groupId) + 1);
        json_object_put(file_json);
    } else {
        groupId = "0";
        memcpy(id, groupId, strlen(groupId) + 1);
        json_object_put(file_json);
    }

    return id;
}

//get cluster id?
/**
* Original method
*/
static const char *get_conflict_json_id(char *conflict, char *resolution) {
    struct json_object *file_json = json_object_from_file(file_names[CONFLICT_INDEX]);
    if (!file_json) { // if file is empty
        if (!resolution) { //resolution is NULL
            return NULL;
        }
        return "1";
    }

    double jaroW = 0;
    const char *groupId = NULL;
    double max_sim = similarity_th;
    double local_max_sim = 0;
    int idCount = 0;

    struct json_object *obj;
    const char *jconf;
    const char *jresol;
    int arraylen;

    double total_similarity = 0;
    json_object_object_foreach(file_json, key, val) {
        obj = NULL;
        arraylen = json_object_array_length(val);
        idCount += 1;
        total_similarity = 0;
	
        	for (int i = 0; i < arraylen; i++) {
            		obj = json_object_array_get_idx(val, i);
            		jconf = json_object_get_string(json_object_object_get(obj, "conflict"));
            		jresol = json_object_get_string(json_object_object_get(obj, "resolution"));
			
            			if (resolution) {
                			if (strcmp(conflict, jconf) == 0 && strcmp(resolution, jresol) == 0) {
                    				json_object_put(file_json);
                    				return NULL;
                			}
            			}	

            			jaroW = jaro_winkler_distance(conflict, jconf);
            			total_similarity += jaroW;
			
        	}
        	double avg = total_similarity / arraylen;
        	if (avg >= local_max_sim) {
            		local_max_sim = avg;
        	}

        	if (avg >= max_sim) {
            		max_sim = avg;
            		groupId = key;
        	}
	
    }
    printf("Regular Local max Similarity: %f\n", local_max_sim);
    printf("Regular MAX Similarity: %f\n", max_sim);

    char *id = malloc(sizeof(char *));
    if (!groupId && resolution) { //if group == null and resolution != null
        //create new group id
        json_object_put(file_json);
        id = (char *) json_object_to_json_string(json_object_new_int(idCount + 1));
    } else if (groupId) { //groupid != null
        memcpy(id, groupId, strlen(groupId) + 1);
        json_object_put(file_json);
    } else {
        groupId = "0";
        memcpy(id, groupId, strlen(groupId) + 1);
        json_object_put(file_json);
    }
    return id;
}

/**
 * This is a version of the method that implements complete linkage
 * @param conflict
 * @param resolution
 * @return
 */
static const char *get_conflict_json_id_maximum(char *conflict, char *resolution) {
    struct json_object *file_json = json_object_from_file(file_names[CONFLICT_INDEX]);
    if (!file_json) { // if file is empty
        if (!resolution) { //resolution is NULL
            return NULL;
        }
        return "1";
    }

    double jaroW = 0;
    const char *groupId = NULL;
    double max_sim = similarity_th;
    double local_max_sim = 0;
    int idCount = 0;

    struct json_object *obj;
    const char *jconf;
    const char *jresol;
    int arraylen;

    double total_similarity = 0;

    double local_maximum=1;
    json_object_object_foreach(file_json, key, val)
    {
        obj = NULL;
        arraylen = json_object_array_length(val);
        idCount += 1;
        total_similarity = 0;
        local_maximum=1;
        for (int i = 0; i < arraylen; i++) {
            obj = json_object_array_get_idx(val, i);
            jconf = json_object_get_string(json_object_object_get(obj, "conflict"));
            jresol = json_object_get_string(json_object_object_get(obj, "resolution"));

            if (resolution) {
                if (strcmp(conflict, jconf) == 0 && strcmp(resolution, jresol) == 0) {
                    json_object_put(file_json);
                    return NULL;
                }
            }

            jaroW = jaro_winkler_distance(conflict, jconf);
            jaroW = jaro_winkler_distance(conflict,jconf);


            if(jaroW<=local_maximum){
                local_maximum=jaroW;
            }
        }

        if (local_maximum >= local_max_sim) {
            local_max_sim = local_maximum;
        }

        if (local_maximum >= max_sim) {
            max_sim = local_maximum;
            groupId = key;
        }
    }
    printf("Regular Local max Similarity: %f\n", local_max_sim);
    printf("Regular MAX Similarity: %f\n", max_sim);

    char *id = malloc(sizeof(char *));
    if (!groupId && resolution) { //if group == null and resolution != null
        //create new group id
        json_object_put(file_json);
        id = (char *) json_object_to_json_string(json_object_new_int(idCount + 1));
    } else if (groupId) { //groupid != null
        memcpy(id, groupId, strlen(groupId) + 1);
        json_object_put(file_json);
    } else {
        groupId = "0";
        memcpy(id, groupId, strlen(groupId) + 1);
        json_object_put(file_json);
    }
    return id;
}


/**
 * This is a version of the method that implements simple linkage
 * @param conflict
 * @param resolution
 * @return
 */
static const char *get_conflict_json_id_minimum(char *conflict, char *resolution) {
    struct json_object *file_json = json_object_from_file(file_names[CONFLICT_INDEX]);
    if (!file_json) { // if file is empty
        if (!resolution) { //resolution is NULL
            return NULL;
        }
        return "1";
    }

    double jaroW = 0;
    const char *groupId = NULL;
    double max_sim = similarity_th;
    double local_max_sim = 0;
    int idCount = 0;

    struct json_object *obj;
    const char *jconf;
    const char *jresol;
    int arraylen;
    double local_minimum_conflict=0;

    double total_similarity = 0;
    json_object_object_foreach(file_json, key, val)
    {
        obj = NULL;
        arraylen = json_object_array_length(val);
        idCount += 1;
        total_similarity = 0;
        local_minimum_conflict=0;
        for (int i = 0; i < arraylen; i++) {
            obj = json_object_array_get_idx(val, i);
            jconf = json_object_get_string(json_object_object_get(obj, "conflict"));
            jresol = json_object_get_string(json_object_object_get(obj, "resolution"));

            if (resolution) {
                if (strcmp(conflict, jconf) == 0 && strcmp(resolution, jresol) == 0) {
                    json_object_put(file_json);
                    return NULL;
                }
            }

            jaroW = jaro_winkler_distance(conflict,jconf);

            if(jaroW>=local_minimum_conflict){
                local_minimum_conflict=jaroW;
            }
        }


        if (local_minimum_conflict >= max_sim) {
            max_sim = local_minimum_conflict;
            groupId = key;
        }


    }
    printf("Regular Local max Similarity: %f\n", local_max_sim);
    printf("Regular MAX Similarity: %f\n", max_sim);

    char *id = malloc(sizeof(char *));
    if (!groupId && resolution) { //if group == null and resolution != null
        //create new group id
        json_object_put(file_json);
        id = (char *) json_object_to_json_string(json_object_new_int(idCount + 1));
    } else if (groupId) { //groupid != null
        memcpy(id, groupId, strlen(groupId) + 1);
        json_object_put(file_json);
    } else {
        groupId = "0";
        memcpy(id, groupId, strlen(groupId) + 1);
        json_object_put(file_json);
    }
    return id;
}

static double get_largest_distance(const struct json_object *val) {

    double total_similarity = 0;
    double jaroW_conf = 0;
    double jaroW_resol = 0;
    struct json_object *obj;
    struct json_object *obj2;
    const char *jconf1;
    const char *jresol1;
    const char *jconf2;
    const char *jresol2;
    int arraylen = json_object_array_length(val);
    double largest_distance = 1;
    if(arraylen<=1){
        return 0;
    }
    for (int i = 0; i < arraylen - 1; i++) {
        obj = json_object_array_get_idx(val, i);
        jconf1 = json_object_get_string(json_object_object_get(obj, "conflict"));
        jresol1 = json_object_get_string(json_object_object_get(obj, "resolution"));

        double subcluster_similarity = 0;
        for (int j = i + 1; j < arraylen; j++) {
            obj2 = json_object_array_get_idx(val, j);
            jconf2 = json_object_get_string(json_object_object_get(obj2, "conflict"));
            jresol2 = json_object_get_string(json_object_object_get(obj2, "resolution"));
            jaroW_conf = jaro_winkler_distance(jconf1, jconf2);
            jaroW_resol = jaro_winkler_distance(jresol1, jresol2);
            double sim = (jaroW_conf + jaroW_resol) / 2;
            if(sim<largest_distance){
                largest_distance=sim;
            }

        }

    }
    return largest_distance;
}

static double get_largest_distance_conflict(const struct json_object *val) {

    double total_similarity = 0;
    double jaroW_conf = 0;
    double jaroW_resol = 0;
    struct json_object *obj;
    struct json_object *obj2;
    const char *jconf1;
    const char *jresol1;
    const char *jconf2;
    const char *jresol2;
    int arraylen = json_object_array_length(val);
    double largest_distance = 1;
    if(arraylen<=1){
        return 0;
    }
    for (int i = 0; i < arraylen - 1; i++) {
        obj = json_object_array_get_idx(val, i);
        jconf1 = json_object_get_string(json_object_object_get(obj, "conflict"));

        double subcluster_similarity = 0;
        for (int j = i + 1; j < arraylen; j++) {
            obj2 = json_object_array_get_idx(val, j);
            jconf2 = json_object_get_string(json_object_object_get(obj2, "conflict"));
            jaroW_conf = jaro_winkler_distance(jconf1, jconf2);
            double sim = jaroW_conf;
            if(sim<largest_distance){
                largest_distance=sim;
            }

        }

    }
    return largest_distance;
}

static double get_largest_distance_resolution(const struct json_object *val) {

    double total_similarity = 0;
    double jaroW_conf = 0;
    double jaroW_resol = 0;
    struct json_object *obj;
    struct json_object *obj2;
    const char *jconf1;
    const char *jresol1;
    const char *jconf2;
    const char *jresol2;
    int arraylen = json_object_array_length(val);
    double largest_distance = 1;
    if(arraylen<=1){
        return 0;
    }
    for (int i = 0; i < arraylen - 1; i++) {
        obj = json_object_array_get_idx(val, i);
        jresol1 = json_object_get_string(json_object_object_get(obj, "resolution"));

        double subcluster_similarity = 0;
        for (int j = i + 1; j < arraylen; j++) {
            obj2 = json_object_array_get_idx(val, j);
            jresol2 = json_object_get_string(json_object_object_get(obj2, "resolution"));
            jaroW_resol = jaro_winkler_distance(jresol1, jresol2);
            double sim = jaroW_resol;
            if(sim<largest_distance){
                largest_distance=sim;
            }

        }

    }
    return largest_distance;
}

static double get_statistic(const struct json_object *val, double *avg_similarity, double *avg_similarity_conflict, double *avg_similarity_resolution,
                                                           double *avg_longest_distance, double *longest_conflict, double *longest_resolution,
							   double *distance_to_latest_conflict, double *distance_to_latest_resolution) {

    double total_similarity = 0;
    double jaroW_conf = 0;
    double jaroW_resol = 0;
    struct json_object *obj;
    struct json_object *obj2;
    const char *jconf1;
    const char *jresol1;
    const char *jconf2;
    const char *jresol2;
    int arraylen = json_object_array_length(val);
    int comparisons = 0;
    double total_comparison = 0;
    double total_similarity_conflict=0;
    double total_similarity_resolution=0;
    double longest_distance=1.0;
    double longest_distance_conf=1.0;
    double longest_distance_res=1.0;
    double distance_to_latest_conf=0.0;
    double distance_to_latest_resol=0.0;
    

    if(arraylen<=1){
        *avg_similarity=(double)1.0;
        *avg_similarity_conflict=(double)1.0;
        *avg_similarity_resolution=(double)1.0;
        *avg_longest_distance=(double)0.0;
        *longest_conflict=(double)0.0;
        *longest_resolution=(double)0.0;
	*distance_to_latest_conflict=(double)0.0;
        *distance_to_latest_resolution=(double)0.0;
        return 1;
    }
    for (int i = 0; i < arraylen - 1; i++) {
        obj = json_object_array_get_idx(val, i);
        jconf1 = json_object_get_string(json_object_object_get(obj, "conflict"));
        jresol1 = json_object_get_string(json_object_object_get(obj, "resolution"));

        double subcluster_similarity = 0;
        for (int j = i + 1; j < arraylen; j++) {
            obj2 = json_object_array_get_idx(val, j);
            jconf2 = json_object_get_string(json_object_object_get(obj2, "conflict"));
            jresol2 = json_object_get_string(json_object_object_get(obj2, "resolution"));
            jaroW_conf = jaro_winkler_distance(jconf1, jconf2);
            jaroW_resol = jaro_winkler_distance(jresol1, jresol2);
            double sim = (jaroW_conf + jaroW_resol) / 2;
            if(sim<longest_distance){
                longest_distance=sim;
            }
            if(jaroW_conf<longest_distance_conf){
                longest_distance_conf=jaroW_conf;
            }
            if(jaroW_resol<longest_distance_res){
                longest_distance_res=jaroW_resol;
            }
            /*Varibles for the ponderated solution, evaluates higher than the direct solution*/
            subcluster_similarity += sim;
            /*variables for direct solution, evaluates equal to excel*/
            total_comparison += sim;
            total_similarity_conflict +=jaroW_conf;
            total_similarity_resolution +=jaroW_resol;
            comparisons++;

        }
        distance_to_latest_conf  += jaroW_conf;
        distance_to_latest_resol += jaroW_resol;
        total_similarity += subcluster_similarity / (arraylen - (i + 1));

    }


    *avg_similarity = (total_comparison / comparisons);
    *avg_similarity_conflict = (total_similarity_conflict / comparisons);
    *avg_similarity_resolution = (total_similarity_resolution / comparisons);
    *avg_longest_distance = longest_distance;
    *longest_conflict = longest_distance_conf;
    *longest_resolution = longest_distance_res;

    *distance_to_latest_conflict  = (double)(distance_to_latest_conf/(arraylen - 1));
    *distance_to_latest_resolution = (double)(distance_to_latest_resol/(arraylen - 1));

    return (total_comparison / comparisons);
}

static double average_similarity(const struct json_object *val) {

    double total_similarity = 0;
    double jaroW_conf = 0;
    double jaroW_resol = 0;
    struct json_object *obj;
    struct json_object *obj2;
    const char *jconf1;
    const char *jresol1;
    const char *jconf2;
    const char *jresol2;
    int arraylen = json_object_array_length(val);
    int comparisons = 0;
    double total_comparison = 0;
    
    if(arraylen<=1){
	return 1;
    }

    for (int i = 0; i < arraylen - 1; i++) {
        obj = json_object_array_get_idx(val, i);
        jconf1 = json_object_get_string(json_object_object_get(obj, "conflict"));
        jresol1 = json_object_get_string(json_object_object_get(obj, "resolution"));

        double subcluster_similarity = 0;
        for (int j = i + 1; j < arraylen; j++) {
            obj2 = json_object_array_get_idx(val, j);
            jconf2 = json_object_get_string(json_object_object_get(obj2, "conflict"));
            jresol2 = json_object_get_string(json_object_object_get(obj2, "resolution"));
            jaroW_conf = jaro_winkler_distance(jconf1, jconf2);
            jaroW_resol = jaro_winkler_distance(jresol1, jresol2);
            double sim = (jaroW_conf + jaroW_resol) / 2;
            /*Varibles for the ponderated solution, evaluates higher than the direct solution*/
            subcluster_similarity += sim;
            /*variables for direct solution, evaluates equal to excel*/
            total_comparison += sim;
            comparisons++;
        }
        total_similarity += subcluster_similarity / (arraylen - (i + 1));

    }

    //printf("%f, %f\n",total_similarity/(arraylen-1),(total_comparison/comparisons));
    //return (total_similarity/(arraylen-1));
    return (total_comparison / comparisons);
}

static double average_similarity_conflict(const struct json_object *val) {

    double total_similarity = 0;
    double jaroW_conf = 0;
    double jaroW_resol = 0;
    struct json_object *obj;
    struct json_object *obj2;
    const char *jconf1;
    const char *jresol1;
    const char *jconf2;
    const char *jresol2;
    int arraylen = json_object_array_length(val);
    int comparisons = 0;
    double total_comparison = 0;
    if(arraylen<=1){
        return 1;
    }
    for (int i = 0; i < arraylen - 1; i++) {
        obj = json_object_array_get_idx(val, i);
        jconf1 = json_object_get_string(json_object_object_get(obj, "conflict"));

        double subcluster_similarity = 0;
        for (int j = i + 1; j < arraylen; j++) {
            obj2 = json_object_array_get_idx(val, j);
            jconf2 = json_object_get_string(json_object_object_get(obj2, "conflict"));
            jaroW_conf = jaro_winkler_distance(jconf1, jconf2);
            double sim = jaroW_conf;
            /*Varibles for the ponderated solution, evaluates higher than the direct solution*/
            subcluster_similarity += sim;
            /*variables for direct solution, evaluates equal to excel*/
            total_comparison += sim;
            comparisons++;
        }
        total_similarity += subcluster_similarity / (arraylen - (i + 1));

    }

    //printf("%f, %f\n",total_similarity/(arraylen-1),(total_comparison/comparisons));
    //return (total_similarity/(arraylen-1));
    return (total_comparison / comparisons);
}

static double average_similarity_resolution(const struct json_object *val) {

    double total_similarity = 0;
    double jaroW_conf = 0;
    double jaroW_resol = 0;
    struct json_object *obj;
    struct json_object *obj2;
    const char *jconf1;
    const char *jresol1;
    const char *jconf2;
    const char *jresol2;
    int arraylen = json_object_array_length(val);
    int comparisons = 0;
    double total_comparison = 0;
    if(arraylen<=1){
        return 1;
    }
    for (int i = 0; i < arraylen - 1; i++) {
        obj = json_object_array_get_idx(val, i);
        jresol1 = json_object_get_string(json_object_object_get(obj, "resolution"));

        double subcluster_similarity = 0;
        for (int j = i + 1; j < arraylen; j++) {
            obj2 = json_object_array_get_idx(val, j);
            jresol2 = json_object_get_string(json_object_object_get(obj2, "resolution"));
            jaroW_resol = jaro_winkler_distance(jresol1, jresol2);
            double sim = jaroW_resol;
            /*Varibles for the ponderated solution, evaluates higher than the direct solution*/
            subcluster_similarity += sim;
            /*variables for direct solution, evaluates equal to excel*/
            total_comparison += sim;
            comparisons++;
        }
        total_similarity += subcluster_similarity / (arraylen - (i + 1));

    }

    //printf("%f, %f\n",total_similarity/(arraylen-1),(total_comparison/comparisons));
    //return (total_similarity/(arraylen-1));
    return (total_comparison / comparisons);
}

static double average_intrasimilarity(const struct json_object *file_json) {
    double avg_total_similarity = 0;
    int arraylen;
    int validClusters = 0;
    int there_is_valid_clusters = 0;


    json_object_object_foreach(file_json, key, val) {
        arraylen = json_object_array_length(val);

        if (arraylen > 1) {
            there_is_valid_clusters = 1;
            validClusters++;
            avg_total_similarity = avg_total_similarity + average_similarity(val);
        }
    }
    double avg = 0;
    if (there_is_valid_clusters == 1) {
        avg = (avg_total_similarity / validClusters);
    }
    return avg;
}

/**
* Original method
**/
static const char *get_conflict_json_id_enhanced(struct json_object *file_json, char *conflict, char *resolution) {

    if (!file_json) { // if file is empty
        if (!resolution) { //resolution is NULL
            return NULL;
        }
        return "1";
    }

    double jaroW = 0;
    double jaroW_resol = 0;
    //size_t leve = 0 ;
    const char *groupId = NULL;
    double max_sim = similarity_th;
    double max_sim_resol = similarity_th;
    double max_avg_similarity =0.8;
    int idCount = 0;

    struct json_object *obj;
    const char *jconf;
    const char *jresol;
    int arraylen;

    double total_similarity = 0;
    double total_similarity_resol = 0;
    json_object_object_foreach(file_json, key, val) {
        obj = NULL;
        arraylen = json_object_array_length(val);
        idCount += 1;
        total_similarity = 0;
        total_similarity_resol = 0;
        for (int i = 0; i < arraylen; i++) {
            obj = json_object_array_get_idx(val, i);
            jconf = json_object_get_string(json_object_object_get(obj, "conflict"));
            jresol = json_object_get_string(json_object_object_get(obj, "resolution"));

            if (resolution) {
                if (strcmp(conflict, jconf) == 0 && strcmp(resolution, jresol) == 0) {
                    return NULL;
                }
            }

            jaroW = jaro_winkler_distance(conflict, jconf);
            total_similarity += jaroW;
            if (resolution) {
                jaroW_resol = jaro_winkler_distance(resolution, jresol);
                total_similarity_resol += jaroW_resol;
            }
        }

        double avg = total_similarity / arraylen;
        double avg_resol = total_similarity_resol / arraylen;

        if (resolution) {
            if (avg >= max_sim && avg_resol >= max_sim_resol) {
                	max_sim = avg;
                	max_sim_resol = avg_resol;
			//max_avg_similarity=(avg+avg_resol)/2;
                	groupId = key;
            }
        } else {
            if (avg >= max_sim) {
                max_sim = avg;
                groupId = key;
            }
        }

    }
    printf("ENHANCED MAX Similarity: %f\n", max_sim);

    char *id = malloc(sizeof(char *));
    //json_object_put(file_json);
    if (!groupId && resolution) { //if group == null and resolution != null
        //create new group id
        //json_object_put(file_json);
        id = (char *) json_object_to_json_string(json_object_new_int(idCount + 1));
    } else if (groupId) { //groupid != null
        memcpy(id, groupId, strlen(groupId) + 1);
        //json_object_put(file_json);
    }
    return id;
}

/*Maximum linkage, search for the most similar conflict and added to the same cluster
 * search on the
 * */
static const char *get_conflict_json_id_enhanced_maximum(struct json_object *file_json, char *conflict, char *resolution) {

    if (!file_json) { // if file is empty
        if (!resolution) { //resolution is NULL
            return  NULL;
        }
        return "1";
    }

    double jaroW = 0 ;
    double jaroW_resol = 0 ;
    //size_t leve = 0 ;
    const char* groupId = NULL;
    double max_sim = similarity_th;
    double max_sim_resol = similarity_th;
    double local_maximum_conflict=1;
    double local_maximum_resolution=1;
    int idCount = 0;

    struct json_object *obj;
    const char* jconf;
    const char* jresol;
    int arraylen;

    double total_similarity = 0;
    double total_similarity_resol = 0;
    json_object_object_foreach(file_json,key,val){
        obj = NULL;
        arraylen = json_object_array_length(val);
        idCount += 1;
        total_similarity = 0;
        total_similarity_resol = 0;
        local_maximum_conflict=1;
        local_maximum_resolution=1;
        for (int i = 0; i < arraylen; i++) {
            obj = json_object_array_get_idx(val, i);
            jconf = json_object_get_string(json_object_object_get(obj, "conflict"));
            jresol = json_object_get_string(json_object_object_get(obj, "resolution"));

            if (resolution) {
                if (strcmp(conflict, jconf) == 0 && strcmp(resolution, jresol) == 0) {
                    //fprintf_ln(stderr,
                    //          _("LOG_EXIT: get_conflict_json_id : conflict and resolution already present in json file\n");
                    //comments because should no be closed as pass as parameter
                    //json_object_put(file_json);
                    return NULL;
                }
            }

            jaroW = jaro_winkler_distance(conflict,jconf);
            if (resolution) {
                jaroW_resol = jaro_winkler_distance(resolution,jresol);
            }

            if(jaroW<=local_maximum_conflict){
                local_maximum_conflict=jaroW;
                if (resolution) {
                    if(jaroW_resol<=local_maximum_resolution){
                        local_maximum_resolution=jaroW_resol;
                    }
                }
            }

        }

        if(resolution){
            if (local_maximum_conflict >= max_sim && local_maximum_resolution>=max_sim_resol) {
                max_sim = local_maximum_conflict;
                max_sim_resol= local_maximum_resolution;
                groupId = key;
            }
        }
        else{
            if (local_maximum_conflict >= max_sim) {
                max_sim = local_maximum_conflict;
                groupId = key;
            }
        }

    }
    if(resolution) {
        printf("ENHANCED MAX Similarity: %f, %f\n", max_sim,max_sim_resol);
    }
    else{
        printf("ENHANCED MAX Similarity: %f\n", max_sim);
    }
    fflush(stdout);
    char * id = malloc(sizeof(char*));
    if (!groupId && resolution) { //if group == null and resolution != null
        id = (char*) json_object_to_json_string(json_object_new_int(idCount+1));
    } else if (groupId) { //groupid != null
        memcpy(id,groupId, strlen(groupId)+1);
    }
    return id;
}

/*Minimum linkage, search for the most similar conflict and added to the same cluster
 * search on the
 * */
static const char *get_conflict_json_id_enhanced_minimum(struct json_object *file_json, char *conflict, char *resolution) {

    if (!file_json) { // if file is empty
        if (!resolution) { //resolution is NULL
            return  NULL;
        }
        return "1";
    }

    double jaroW = 0 ;
    double jaroW_resol = 0 ;
    //size_t leve = 0 ;
    const char* groupId = NULL;
    double max_sim = similarity_th;
    double max_sim_resol = similarity_th;
    double local_minimum_conflict=0;
    double local_minimum_resolution=0;
    int idCount = 0;

    struct json_object *obj;
    const char* jconf;
    const char* jresol;
    int arraylen;

    double total_similarity = 0;
    double total_similarity_resol = 0;
    json_object_object_foreach(file_json,key,val){
        obj = NULL;
        arraylen = json_object_array_length(val);
        idCount += 1;
        total_similarity = 0;
        total_similarity_resol = 0;
        local_minimum_conflict=0;
        local_minimum_resolution=0;
        for (int i = 0; i < arraylen; i++) {
            obj = json_object_array_get_idx(val, i);
            jconf = json_object_get_string(json_object_object_get(obj, "conflict"));
            jresol = json_object_get_string(json_object_object_get(obj, "resolution"));

            if (resolution) {
                if (strcmp(conflict, jconf) == 0 && strcmp(resolution, jresol) == 0) {
                    //fprintf_ln(stderr,
                    //          _("LOG_EXIT: get_conflict_json_id : conflict and resolution already present in json file\n");
                    //comments because should no be closed as pass as parameter
                    //json_object_put(file_json);
                    return NULL;
                }
            }

            jaroW = jaro_winkler_distance(conflict,jconf);
            if (resolution) {
                jaroW_resol = jaro_winkler_distance(resolution,jresol);
            }

            if(jaroW>=local_minimum_conflict){
                local_minimum_conflict=jaroW;
                if (resolution) {
                    if(jaroW_resol>=local_minimum_resolution){
                        local_minimum_resolution=jaroW_resol;
                    }
                }
            }

        }

        if(resolution){
            if (local_minimum_conflict >= max_sim && local_minimum_resolution>=max_sim_resol) {
                max_sim = local_minimum_conflict;
                max_sim_resol= local_minimum_resolution;
                groupId = key;
            }
        }
        else{
            if (local_minimum_conflict >= max_sim) {
                max_sim = local_minimum_conflict;
                groupId = key;
            }
        }

    }
    if(resolution) {
        printf("ENHANCED MAX Similarity: %f, %f\n", max_sim,max_sim_resol);
    }
    else{
        printf("ENHANCED MAX Similarity: %f\n", max_sim);
    }
    fflush(stdout);
    char * id = malloc(sizeof(char*));
    if (!groupId && resolution) { //if group == null and resolution != null
        id = (char*) json_object_to_json_string(json_object_new_int(idCount+1));
    } else if (groupId) { //groupid != null
        memcpy(id,groupId, strlen(groupId)+1);
    }
    return id;
}



static int write_json_object(struct json_object *file_object, char *file_name, const char *group_id, char *conflict,
                             char *resolution) {
    printf("Login: write_json_object\n");
    struct json_object *object = json_object_new_object();
    struct json_object *jarray = json_object_new_array();

    jarray = json_object_object_get(file_object, group_id);

    if (!jarray)
        jarray = json_object_new_array();

    if (json_object_array_length(jarray)) { // get object id1 if exists
        //add new line to object id1
        json_object_object_add(object, "conflict", json_object_new_string(conflict));
        json_object_object_add(object, "resolution", json_object_new_string(resolution));
        json_object_array_add(jarray, object);
    } else { // if id1 not exists
        json_object_object_add(object, "conflict", json_object_new_string(conflict));
        json_object_object_add(object, "resolution", json_object_new_string(resolution));
        json_object_array_add(jarray, object);
        json_object_object_add(file_object, group_id, jarray);
    }

    FILE *fp = fopen(file_name, "w");
    if (!fp) {
        perror("Error");
        printf("Value of errno: %d(%s)\n", errno, strerror(errno));
        printf("Exit: write_json_object: not open FILE conflict_index\n");
        exit(-666);
        return 0;
    }
    //update or add groupid to file
    fprintf(fp, "%s", json_object_to_json_string_ext(file_object, 2));
    fclose(fp);
    //free(object);
    //free(jarray);
    printf("Exit: write_json_object\n");
    return 1;
}

/* Create dynamic 2-d array of size x*y.  */
double **create_array(int x, int y) {
    int i;
    double **arr = (double **) malloc(sizeof(double *) * x);
    for (i = 0; i < x; i++) {
        arr[i] = (double *) malloc(sizeof(double) * y);
        memset(arr[i], -1, sizeof(arr[i]));
    }
    return arr;
}

/* Deallocate memory.  */
void remove_array(double **arr, int x) {
    int i;
    for (i = 0; i < x; i++)
        free(arr[i]);
    free(arr);
}

static struct json_object *hierarchical_clustering2(const struct json_object *json_conflict) {
    struct json_object *file_json = json_object_new_object();
    struct json_object *jarray = json_object_new_array();
    struct json_object *schedule_delete = json_object_new_array();
    int idCount = 0;

    int cl = json_object_array_length(json_conflict);
    //double cluster_weights[cl][cl];
    double **cluster_weights = create_array(cl, cl);
    for (int i = 0; i < cl; i++) {
        jarray = json_object_new_array();
        struct json_object *tempElement = json_object_get(json_object_array_get_idx(json_conflict, i));
        json_object_array_add(jarray, tempElement);
        //json_object_get(tempElement);
        char *id = malloc(sizeof(char *));
        id = (char *) json_object_to_json_string(json_object_new_int(i + 1));
        json_object_object_add(file_json, id, jarray);
        //for (int j=0;j<cl;j++){
        //    cluster_weights[i][j]=-1;
        //}
    }
    //printf("new clusters: %s\n",json_object_to_json_string_ext(file_json,2));
    //json_object_put(json_conflict);

    for (int i = 0; i < cl; i++) {
        char array[3];
        sprintf(array, "%d", (i + 1));
        struct json_object *returnObjX = json_object_object_get(file_json, array);
        for (int j = 0; j < cl; j++) {
            if (j > i) {
                char array2[3];
                sprintf(array2, "%d", (j + 1));
                struct json_object *returnObjY = json_object_object_get(file_json, array2);
                cluster_weights[i][j] = cluster_cluster_similarity(returnObjX, returnObjY);
            }
        }
    }

    int there_is_maximum = 1;
    while (there_is_maximum == 1) {
        double max = -1;
        int max_i = 0;
        int max_j = 0;
        for (int i = 0; i < cl; i++) {
            for (int j = 0; j < cl; j++) {
                if (j > i) {
                    if (cluster_weights[i][j] > max) {
                        max = cluster_weights[i][j];
                        max_i = i;
                        max_j = j;
                    }
                }
            }
        }
        //printf("Maximum[%d][%d]: %f\n",max_i,max_j,max);
        if (max > similarity_th) {
            char *key_i = malloc(sizeof(char *));
            key_i = (char *) json_object_to_json_string(json_object_new_int(max_i + 1));
            char *key_j = malloc(sizeof(char *));
            key_j = (char *) json_object_to_json_string(json_object_new_int(max_j + 1));

            struct json_object *cluster_i = json_object_object_get(file_json, key_i);
            struct json_object *cluster_j = json_object_object_get(file_json, key_j);

            int length_cluster_i = json_object_array_length(cluster_i);
            int length_cluster_j = json_object_array_length(cluster_j);

            for (int j = 0; j < length_cluster_j; j++) {
                struct json_object *temp_element = json_object_get(json_object_array_get_idx(cluster_j, j));
                json_object_array_add(cluster_i, temp_element);
                json_object_put(temp_element);
            }
            json_object_array_add(schedule_delete, json_object_new_int(max_j + 1));

            for (int i = 0; i < cl; i++) {
                cluster_weights[i][max_j] = -1;
                cluster_weights[max_j][i] = -1;
            }

            for (int i = 0; i < cl; i++) {
                char *key_temp = malloc(sizeof(char *));
                key_temp = (char *) json_object_to_json_string(json_object_new_int(i + 1));
                struct json_object *cluster_temp = json_object_object_get(file_json, key_temp);
                if (i < max_i && cluster_weights[i][max_i] != -1) {
                    cluster_weights[i][max_i] = cluster_cluster_similarity(cluster_i, cluster_temp);
                }

                if (i > max_i && cluster_weights[max_i][i] != -1) {
                    cluster_weights[max_i][i] = cluster_cluster_similarity(cluster_i, cluster_temp);
                }
            }
        } else {
            there_is_maximum = 0;
        }
    }
    remove_array(cluster_weights, cl);
    struct json_object *cluster_result = json_object_new_object();
    int key_index = 1;
    int del_flag = 1;
    json_object_object_foreach(file_json, key, val) {
        del_flag = 1;
        for (int i = 0; i < json_object_array_length(schedule_delete); i++) {
            char *key_i = malloc(sizeof(char *));
            key_i = (char *) json_object_to_json_string(json_object_array_get_idx(schedule_delete, i));
            if (strcmp(key, key_i) == 0) {
                del_flag = 0;
            }
        }
        if (del_flag == 1) {
            char *id = malloc(sizeof(char *));
            id = (char *) json_object_to_json_string(json_object_new_int(key_index));
            json_object_object_add(cluster_result, id, json_object_get(val));
            key_index++;
        }
    }

    json_object_put(jarray);
    json_object_put(schedule_delete);
    //printf("........................................................................ file_json is null?\n");
    //printf("new clusters final.........:\n %s\n",json_object_to_json_string_ext(cluster_result,2));
    //printf("........................................................................ NOT NULL\n");
    return cluster_result;
}

static struct json_object *hierarchical_clustering(const struct json_object *json_conflict) {
    struct json_object *file_json = json_object_new_object();
    struct json_object *jarray = json_object_new_array();
    struct json_object *obj1;
    struct json_object *obj2;
    int idCount = 0;
    const char *jconf1;
    const char *jresol1;
    const char *jconf2;
    const char *jresol2;
    double jaroW_conf = 0;
    double jaroW_resol = 0;
    double total_similarity = 0;
    double max_sim = similarity_th;
    double max_cluster_sim = similarity_th;
    int max_sim_index = -1;

    int cl = json_object_array_length(json_conflict);
    int cluster_flags[cl];
    for (int i = 0; i < cl; i++) {
        cluster_flags[i] = 0;
    }
    int i = 0;
    while (i < cl) {
        obj1 = json_object_array_get_idx(json_conflict, i);
        jconf1 = json_object_get_string(json_object_object_get(obj1, "conflict"));
        jresol1 = json_object_get_string(json_object_object_get(obj1, "resolution"));
        max_sim = similarity_th;
        max_cluster_sim = similarity_th;
        max_sim_index = -1;
        for (int j = 0; j < cl; j++) {
            if (i != j) {
                obj2 = json_object_array_get_idx(json_conflict, j);
                jconf2 = json_object_get_string(json_object_object_get(obj2, "conflict"));
                jresol2 = json_object_get_string(json_object_object_get(obj2, "resolution"));
                jaroW_conf = jaro_winkler_distance(jconf1, jconf2);
                jaroW_resol = jaro_winkler_distance(jresol1, jresol2);
                total_similarity = (jaroW_conf + jaroW_resol) / 2;
                if (total_similarity >= max_sim) {
                    if (cluster_flags[i] == 0 && cluster_flags[j] == 0) {
                        max_sim = total_similarity;
                        max_sim_index = j;
                    } else if (cluster_flags[i] != 0 && cluster_flags[j] == 0) {
                        char *id = malloc(sizeof(char *));
                        id = (char *) json_object_to_json_string(json_object_new_int(cluster_flags[i]));
                        jarray = json_object_object_get(file_json, id);
                        int size = json_object_array_length(jarray);
                        double cluster_conflict_sim = 0;
                        double cluster_resol_sim = 0;
                        for (int k = 0; k < size; k++) {
                            struct json_object *conflict = json_object_array_get_idx(jarray, k);
                            const char *conflict_string = json_object_get_string(
                                    json_object_object_get(conflict, "conflict"));
                            const char *resol_string = json_object_get_string(
                                    json_object_object_get(conflict, "resolution"));
                            cluster_conflict_sim =
                                    cluster_conflict_sim + jaro_winkler_distance(jconf2, conflict_string);
                            cluster_resol_sim = cluster_resol_sim + jaro_winkler_distance(jresol2, resol_string);
                        }
                        double cluster_avg_sim = ((cluster_conflict_sim / size) + (cluster_resol_sim / size)) / 2;
                        if (cluster_avg_sim >= max_cluster_sim) {
                            max_sim = total_similarity;
                            max_cluster_sim = cluster_avg_sim;
                            max_sim_index = j;
                        }
                    } else if (cluster_flags[i] == 0 && cluster_flags[j] != 0) {
                        char *id = malloc(sizeof(char *));
                        id = (char *) json_object_to_json_string(json_object_new_int(cluster_flags[j]));
                        jarray = json_object_object_get(file_json, id);
                        int size = json_object_array_length(jarray);
                        double cluster_conflict_sim = 0;
                        double cluster_resol_sim = 0;
                        for (int k = 0; k < size; k++) {
                            struct json_object *conflict = json_object_array_get_idx(jarray, k);
                            const char *conflict_string = json_object_get_string(
                                    json_object_object_get(conflict, "conflict"));
                            const char *resol_string = json_object_get_string(
                                    json_object_object_get(conflict, "resolution"));
                            cluster_conflict_sim =
                                    cluster_conflict_sim + jaro_winkler_distance(jconf1, conflict_string);
                            cluster_resol_sim = cluster_resol_sim + jaro_winkler_distance(jresol1, resol_string);
                        }
                        double cluster_avg_sim = ((cluster_conflict_sim / size) + (cluster_resol_sim / size)) / 2;
                        if (cluster_avg_sim >= max_cluster_sim) {
                            max_sim = total_similarity;
                            max_cluster_sim = cluster_avg_sim;
                            max_sim_index = j;
                        }
                    } else if (cluster_flags[i] == cluster_flags[j] && cluster_flags[i] != 0) {
                        max_sim = total_similarity;
                        max_sim_index = j;
                    }

                }
            }
        }

        idCount = 0;
        json_object_object_foreach(file_json, key, val) {
            idCount += 1;
        }
        if (max_sim_index == -1) {//case 1 no similar conflicts
            jarray = json_object_new_array();
            json_object_array_add(jarray, obj1);
            char *id = malloc(sizeof(char *));
            id = (char *) json_object_to_json_string(json_object_new_int(idCount + 1));
            json_object_object_add(file_json, id, jarray);
            cluster_flags[i] = idCount + 1;
        } else if (cluster_flags[i] == 0 && cluster_flags[max_sim_index] == 0) {//case 2: 2 new similar clusters
            jarray = json_object_new_array();
            json_object_array_add(jarray, obj1);
            obj2 = json_object_array_get_idx(json_conflict, max_sim_index);
            json_object_array_add(jarray, obj2);
            char *id = malloc(sizeof(char *));
            id = (char *) json_object_to_json_string(json_object_new_int(idCount + 1));
            json_object_object_add(file_json, id, jarray);
            cluster_flags[i] = idCount + 1;
            cluster_flags[max_sim_index] = idCount + 1;
        } else if (cluster_flags[i] == 0 && cluster_flags[max_sim_index] != 0) { //case 3: c1 not clustered
            char *id = malloc(sizeof(char *));
            id = (char *) json_object_to_json_string(json_object_new_int(cluster_flags[max_sim_index]));
            jarray = json_object_object_get(file_json, id);
            json_object_array_add(jarray, obj1);
            cluster_flags[i] = cluster_flags[max_sim_index];
        } else if (cluster_flags[i] != 0 && cluster_flags[max_sim_index] == 0) { //case 3: c1 not clustered
            char *id = malloc(sizeof(char *));
            id = (char *) json_object_to_json_string(json_object_new_int(cluster_flags[i]));
            jarray = json_object_object_get(file_json, id);
            obj2 = json_object_array_get_idx(json_conflict, max_sim_index);
            json_object_array_add(jarray, obj2);
            cluster_flags[max_sim_index] = cluster_flags[i];
        }
        //printf("Pairs: %d - %d : %f\n",i,max_sim_index,max_sim);
        i++;
    }

    //printf("new clusters: %s\n",json_object_to_json_string_ext(file_json,2));
    //printf("Indexes: \n");
    //for(int i=0;i<cl;i++){
    //printf("%d,",cluster_flags[i]);
    //}
    //printf("\n");
    //printf("Average Intrasimilarity2: %f\n",average_intrasimilarity(file_json));
    /*int total=0;
    json_object_object_foreach(file_json,key,val){
        obj1 = NULL;
        jarray = json_object_new_array();
        int arraylen = json_object_array_length(val);
        //printf("cluster %d: %d \n",key,arraylen);
        total=total+arraylen;
    }*/
    //printf("Conflict extraction size: %d\n",total);
    return file_json;
}

static struct json_object *recluster(const struct json_object *file_json) {
    struct json_object *obj;
    struct json_object *obj2;
    struct json_object *jarray = json_object_new_array();
    struct json_object *jarray2 = json_object_new_array();
    int arraylen;

    json_object_object_foreach(file_json, key, val) {
        obj = NULL;
        arraylen = json_object_array_length(val);
        for (int i = 0; i < arraylen; i++) {
            obj = json_object_array_get_idx(val, i);

            json_object_array_add(jarray, json_object_get(obj));
        }
    }
    //printf("Conflict extraction size: %d\n",json_object_array_length(jarray));

    /*FILE *fp = fopen("C:\\Users\\Sergio\\Documents\\AlmostRERERE\\intracluster\\conflict.json","w");
    if (!fp){
        printf("Exit: write_json_object: not open FILE conflict_index\n");
        //return 0;
    }
    fprintf(fp,"%s", json_object_to_json_string_ext(jarray,2));
    fclose(fp);
    */
    //printf("randomizing: \n");
    /*int size = json_object_array_length(jarray);
    while(size>0){
        obj = NULL;
        srand(time(NULL));
        int nMax=json_object_array_length(jarray)-1;
        int nRandonNumber = rand()%((nMax+1)-0) + 0;
        obj2 = json_object_array_get_idx(jarray,nRandonNumber);

        json_object_array_add(jarray2,json_object_get(obj2));

        json_object_array_del_idx(jarray,nRandonNumber,1);
        size= json_object_array_length(jarray);
    }*/
    //printf("Conflict radomization size: %d\n",json_object_array_length(jarray2));

    /*FILE *fp2 = fopen("C:\\Users\\Sergio\\Documents\\AlmostRERERE\\intracluster\\conflict_random.json","w");
    if (!fp2){
        printf("Exit: write_json_object: not open FILE conflict_index\n");
        //return 0;
    }
    fprintf(fp2,"%s", json_object_to_json_string_ext(jarray2,2));
    fclose(fp2);*/
    //printf("Randomization finished... \n");
    //json_object_put(jarray);
    json_object_put(jarray2);
    //return hierarchical_clustering(jarray);
    return hierarchical_clustering2(jarray);
}

static void move_file(int b) {
    FILE *source, *target;
    char ch;
    source = fopen(file_names[CONFLICT_INDEX], "r");
    if (source == NULL) {
        return;
    }
    char str[100];
    sprintf(str, "%s%d", "conflict_index", b);
    sprintf(str, "%s%s", str, ".json");
    target = fopen(str, "w");
    if (target == NULL) {
        fclose(source);
        return;
    }
    while ((ch = fgetc(source)) != EOF)
        fputc(ch, target);

    printf("File copied successfully.\n");

    fclose(source);
    fclose(target);
}

static void move_file2() {
    FILE *source, *target;
    char ch;
    source = fopen(file_names[CONFLICT_INDEX], "r");
    if (source == NULL) {
        return;
    }
    char str[100];
    sprintf(str, "%s", file_names[CONFLICT_INDEX_RECLUSTER]);
    //sprintf(str, "%s%s", str, ".json");
    target = fopen(str, "w");
    if (target == NULL) {
        fclose(source);
        return;
    }
    while ((ch = fgetc(source)) != EOF)
        fputc(ch, target);

    printf("File copied successfully.\n");

    fclose(source);
    fclose(target);
}


static int check_for_recluster(int conflict_number) {
    printf("Log: check if clustering is required...\n");
    struct json_object *file_json = json_object_from_file(file_names[CONFLICT_INDEX]);
    double intracluster_similarity = average_intrasimilarity(file_json);
    printf("Average Intrasimilarity: %f\n", intracluster_similarity);
    int id = 0;
    //id = (char*) json_object_to_json_string(json_object_new_int(0));
    if (intracluster_similarity <= intrasimilarity_th) { //check intrasimilatiry threshold
        if (intracluster_similarity > 0) {
            int cluster_count = 0;
            int clusters_lenght_1 = 0;
            int x1 = 0;
            json_object_object_foreach(file_json, key, val) {
                cluster_count += 1;
                int arraylen1 = json_object_array_length(val);
                x1 = x1 + arraylen1;
                if (arraylen1 < 2) {
                    clusters_lenght_1 += 1;
                }
            }
            double percentage_of_singleclusters = (double) clusters_lenght_1 / cluster_count;
            double percentage_wrt_last = 0;
            int diff_first_time = 0;
            if (cluster_population != 0) {
                percentage_wrt_last = (double) (((x1 - cluster_population) * 100) / cluster_population);
            } else {
                diff_first_time = (x1 - cluster_population);
            }
            //printf("Clusters: %d - 1-elementcluster: %d - Percentage: %f\n",cluster_count,clusters_lenght_1,percentage_of_singleclusters);
            printf("Clusters: %d - 1-elementcluster: %d - Percentage: %f - Conflicts in Cluster: %d - Diff from last: %d - change percentage: %f \n",
                   cluster_count, clusters_lenght_1, percentage_of_singleclusters, x1, (x1 - cluster_population),
                   percentage_wrt_last);
            //if(percentage_of_singleclusters < valid_cluster_th){//Start reclustering
            if (percentage_of_singleclusters < valid_cluster_th && (percentage_wrt_last >= 10 || diff_first_time >=
                                                                                                 250)/*(x1-cluster_population)>250*/) {//Start reclustering
                cluster_population = x1;
                diff_first_time = 0;
                printf("Start reclustering...\n");
                struct json_object *file_json_reclustered = recluster(file_json);

                json_object_put(file_json); //close original json object
                int total = 0;
                int number_keys = 0;
                //char ids[256];
                json_object_object_foreach(file_json_reclustered, key,
                                           val) {//simple check for number of conflicts parity
                    //obj1 = NULL;
                    //jarray = json_object_new_array();
                    int arraylen = json_object_array_length(val);
                    //printf("cluster %d: %d \n",key,arraylen);
                    total = total + arraylen;
                    number_keys++;
                }
                //printf("Conflict extraction size: %d\n",total);
                double intracluster_similarity_after = average_intrasimilarity(file_json_reclustered);
                printf("Average Intrasimilarity after reclustering: %f - %d\n", intracluster_similarity_after, total);
                if (intracluster_similarity_after >/*.87*/intracluster_similarity) {
                    printf("Recluster improved, applying changes...\n");
                    move_file(conflict_number);// backup the old conflict index file

                    FILE *fp = fopen(file_names[CONFLICT_INDEX], "w");//write the new recluster index file
                    if (!fp) {
                        printf("Exit: write_json_object: not open FILE conflict_index\n");
                        //return 0;
                    }
                    fprintf(fp, "%s", json_object_to_json_string_ext(file_json_reclustered, 2));
                    fclose(fp);
                    json_object_put(file_json_reclustered);
                    move_file2();
                    /*Prepare to call the regex generator*/
                    int ids_size = 0;
                    if (number_keys <= 9) {
                        ids_size = 2 * number_keys;
                    } else if (number_keys >= 10 && number_keys <= 99) {
                        ids_size = 18 + (number_keys - 9) * 3;
                    } else {
                        ids_size = 18 + 270 + (number_keys - 99) * 4;
                    }

                    char ids[ids_size];
                    ids[0] = '\0';
                    for (int l = 1; l <= number_keys; l++) {
                        char array[3] = {'\0'};
                        sprintf(array, "%d", l);
                        //printf( "%s\n", array );
                        strcat(ids, array);
                        strcat(ids, " ");
                    }
                    printf("IDS for regex: %s \n", ids);
                    //char * id_param = malloc(sizeof(char*));
                    //memcpy(id_param,ids, strlen(ids)+1);
                    //executeRegexJarOnline(ids);

                    //json_object_put(file_json_reclustered);
                    //for(int l = 1; l <= number_keys; l++){
                    //char array[3]= {'\0'};
                    //sprintf(array, "%d", l);
                    //executeRegexJar(array);
                    //sleep(10);
                    //}
                    printf("Reclustering finished...\n");
                    // return id_param;
                    return number_keys;
                } else {
                    printf("Recluster NOT improved...\n");
                    json_object_put(file_json_reclustered);
                    //json_object_put(file_json);
                    return id;
                }
            } else {
                json_object_put(file_json);
                return id;
            }
        } else {
            json_object_put(file_json);
            return id;
        }
    } else {
        json_object_put(file_json);
        return id;
    }

}

static void append_stats(const char *group_id, double intrasimilarity, double largest_distance,double intrasimilarity_conflict,double intrasimilarity_resolution,double longest_distance_conflict, double longest_distance_resolution,
			 double avg_distance_to_latest_conf, double avg_distance_to_latest_resol){
    FILE *fp1;
    //char buf[100];
    fp1 = fopen(file_names[CLUSTER_STATISTICS], "a+");
    if (fp1 == NULL)
        exit(EXIT_FAILURE);
    //snprintf(buf, 12, "\"%s\",\"%f\",\"%f\"\n", group_id, intrasimilarity, largest_distance); // puts string into buffer
    fprintf(fp1, "\"%s\", \"%d\",\"%f\",\"%f\",\"%f\",\"%f\",\"%f\",\"%f\",\"%f\",\"%f\"\n", group_id,conf_ID, intrasimilarity, largest_distance,intrasimilarity_conflict,intrasimilarity_resolution,longest_distance_conflict, longest_distance_resolution,avg_distance_to_latest_conf, avg_distance_to_latest_resol);
    //printf("%s\n", buf);
    fclose(fp1);
}

static int write_json_conflict_index(char *conflict, char *resolution, int conflict_number) {
    printf("Login: write_json_conflict_index\n");
    struct json_object *file_json = json_object_from_file(file_names[CONFLICT_INDEX]);
    size_t cluster_size = 0;

    if (!file_json) // if file is empty
        file_json = json_object_new_object();

    const char *group_id = get_conflict_json_id_enhanced(file_json, conflict, resolution);

    if (!group_id) {
        printf("Exit: write_json_conflict_index NO groupID------------ENHANCED------------\n");
        json_object_put(file_json);
        return 0;
    } else {
        printf("Exit: write_json_conflict_index ENHANCED %s\n", group_id);
    }

    if (!write_json_object(file_json, file_names[CONFLICT_INDEX], group_id, conflict,
                           resolution)) { //if return 0
        printf("Exit: write_json_conflict_index: write json object error\n");
        json_object_put(file_json);
        return 0;
    }

    /**
     * ---------------------------------------------------------------------
     * **/
    struct json_object *jarray = json_object_new_array();
    jarray = json_object_object_get(file_json, group_id);
    double intrasimilarity;
    double intrasimilarity_conflict;
    double intrasimilarity_resolution;
    double longest_intra_distance;
    double longest_distance_conflict;
    double longest_distance_resolution;
    double avg_distance_to_latest_conf;
    double avg_distance_to_latest_resol;

    get_statistic(jarray, &intrasimilarity, &intrasimilarity_conflict, &intrasimilarity_resolution,
                          &longest_intra_distance, &longest_distance_conflict, &longest_distance_resolution,
			  &avg_distance_to_latest_conf, &avg_distance_to_latest_resol);

    printf("Cluster %s - Intra similarity: %f - Longest Distance: %f \n", group_id,intrasimilarity,longest_intra_distance);
    append_stats(group_id,intrasimilarity,longest_intra_distance,intrasimilarity_conflict,intrasimilarity_resolution,longest_distance_conflict, longest_distance_resolution,avg_distance_to_latest_conf, avg_distance_to_latest_resol);
    /**------------------------------------------------------------------------------*/
    struct json_object *cluster_object = json_object_object_get(file_json, group_id);

    if (cluster_object)
        cluster_size = json_object_array_length(cluster_object);

    //printf("\n groupid: %s\n",group_id);
    //const char* ids=check_for_recluster(conflict_number);
    //Uncomment to test without recluster
    int ids = 0;//check_for_recluster(conflict_number);
    //uncomment to test with reclustering
    //int ids=check_for_recluster(conflict_number);

    //printf("ids=%s, strcmp=%d",ids,strcmp(ids, "0"));
    //if(strcmp(ids, "0")==0){ //Check if reclustering is needed.
    if (ids == 0) {
        json_object_put(file_json);
        printf("Starting 'executeRegexJar' with cluster of size %zu\n", cluster_size);
        executeRegexJar(group_id, 0, cluster_size);
        //json_object_put(file_json);
    } else {
        json_object_put(file_json);
        for (int l = 1; l <= ids; l++) {
            //char array[3]= {'\0'};
            //sprintf(array, "%d", l);
            char *array = malloc(sizeof(char *));
            array = (char *) json_object_to_json_string(json_object_new_int(l));
            executeRegexJar(array, l, cluster_size);
            //sleep(10);
        }

        //executeRegexJar(ids);
        printf("Waiting for REGEXJAR...\n");
        //json_object_put(file_json);
        //sleep(1200);
    }

    //executeRegexJar(group_id);

    //Check it reclustering is needed.
    //check_for_recluster(conflict_number);


    //json_object_put(file_json);
    //free(conflict_list);
    printf("Exit: write_json_conflict_index\n");
    return 1;
}

static void regex_replace_suggestion(char *conflict, char *resolution, int jid, char *jv2, char *jdec) {
    printf("Enter: regex_replace_suggestion\n");
    const char *groupId = get_conflict_json_id(conflict, NULL);
    time_t regex_replacement_start;
    time_t regex_replacement_end;

    if (!groupId || strcmp(groupId, "0") == 0) {
        int x = (strcmp(conflict, "") == 0);
        int y = (strcmp(resolution, "") == 0);

        printf("conf: '%s'\n", conflict);
        printf("resol: '%s'\n", resolution);
        printf("conflict and resolution are empty:%d,%d\n", x, y);

        if (strcmp(conflict, "") == 0 && strcmp(resolution, "") == 0) {
            groupId = get_conflict_json_id_empty(conflict, NULL);
            printf("CASE EMPTY CONFLICT AND RESOLUTION   ----regular---- %s\n", groupId);
        } else if (strcmp(conflict, "") == 0 && strcmp(resolution, "") == 1) {
            groupId = get_conflict_json_id_empty_conf(resolution, NULL);
            printf("CASE EMPTY CONFLICT AND WITH RESOLUTION   ----regular---- %s\n", groupId);
        } else {
            //nothing to do
            printf("Non of the cases   ----regular---- %s\n", groupId);
        }
    }

    if (!groupId) {
        printf("Exit: regex_replace_suggestion: No GroupId-----------------regular--------------------\n");
        return;
    } else {
        printf("Group is , and continues   ----regular---- %s\n", groupId);
    }

    struct json_object *file_json = json_object_from_file(file_names[CONFLICT_INDEX]);
    struct json_object *cluster_object = json_object_object_get(file_json, groupId);

    time(&regex_replacement_start);
    pid_t pid = fork();
    if (pid == 0) { // child process
        /* open /dev/null for writing */
        int fd = open(file_names[STRING_REPLACE], O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
        dup2(fd, 1);    /* make stdout a copy of fd (> /dev/null) */
        //dup2(fd, 2);    /* ...and same with stderr */
        close(fd);

        if (conflict == '\0') { //TODO check
            const char *tempConflict = " ";
            execl("/usr/bin/java", "/usr/bin/java", "-jar", REGEX_REPLACEMENT_JAR, "./", groupId, tempConflict,
                  (char *) 0);
        } else {
            execl("/usr/bin/java", "/usr/bin/java", "-jar", REGEX_REPLACEMENT_JAR, "./", groupId, conflict,
                  (char *) 0);
        }
    } else { //parent process
        int status;
        (void) waitpid(pid, &status, 0);
        //printf("\nJar Process terminate with code: %d\n",status);
        time(&regex_replacement_end);
        printf("RegexReplacement executed in %.f sec\n", difftime(regex_replacement_end, regex_replacement_start));
        if (!status) {
            FILE *fp = fopen(file_names[STRING_REPLACE], "r");
            if (!fp)
                return;

            fseek(fp, 0, SEEK_END); // goto end of file
            if (ftell(fp) == 0) {
                printf("file is empty\n");
                unlink(file_names[STRING_REPLACE]);
                printf("Exit: regex_replace_suggestion\n");
                return;
            }
            fseek(fp, 0, SEEK_SET);

            char buffer1[500], buffer2[500], buffer3[500];
            char *regex1 = NULL, *regex2 = NULL, *replace1 = NULL, *replace2 = NULL, *res1 = NULL, *res2 = NULL, *cluster = NULL;
            // read first resolution

            if (fgets(buffer1, 500, fp) && fgets(buffer2, 500, fp) && fgets(buffer3, 500, fp)) {
                regex1 = buffer1;
                replace1 = buffer2;
                res1 = buffer3;
                //fprintf_ln(stderr, _("Regex: %s"), buf.buf);
                printf("Regex1: %s\n", regex1);
                printf("Repla1: %s\n", replace1);
                printf("Resol1: %s\n", res1);
            }

            // read second resolution
            if (fgets(buffer1, 500, fp) && fgets(buffer2, 500, fp) && fgets(buffer3, 500, fp)) {
                regex2 = buffer1;
                replace2 = buffer2;
                res2 = buffer3;
                //fprintf_ln(stderr, _("Regex: %s"), buf.buf);
                printf("Regex2: %s\n", regex2);
                printf("Repla2: %s\n", replace2);
                printf("Resol2: %s\n", res2);
            }
            fclose(fp);

            fp = fopen(file_names[STRING_REPLACE_RESULT], "a+");
            conflict = escapeCSV(conflict);
            jv2 = escapeCSV(jv2);
            //jv1= escapeCSV(jv1);
            cluster = escapeCSV(json_object_get_string(cluster_object));
            if (res1 && res2) {
                double jw1 = jaro_winkler_distance(resolution, res1);
                double jw2 = jaro_winkler_distance(resolution, res2);
                if (jw1 >= jw2) {
                    regex1[strcspn(regex1, "\n")] = 0;
                    replace1[strcspn(replace1, "\n")] = 0;
                    res1[strcspn(res1, "\n")] = 0;
                    regex1 = escapeCSV(regex1);
                    replace1 = escapeCSV(replace1);
                    res1 = escapeCSV(res1);
                    resolution = escapeCSV(resolution);
                    //fprintf(fp,"\"%s\",\"%s\",\"%f\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%d\"\n",conflict,groupId,jw1,regex1,replace1,resolution,res1,jv1,jv2,jdec,jid);
                    fprintf(fp, "\"%s\",\"%s\",\"%f\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%d\",\"%s\"\n", conflict,
                            groupId, jw1, regex1, replace1, resolution, res1, jv2, jdec, jid, cluster);
                    free(conflict);
                    free(regex1);
                    free(replace1);
                    free(res1);
                    free(cluster);
                } else {
                    regex2 = escapeCSV(regex2);
                    replace2 = escapeCSV(replace2);
                    res2 = escapeCSV(res2);
                    regex2[strcspn(regex2, "\n")] = 0;
                    replace2[strcspn(replace2, "\n")] = 0;
                    res2[strcspn(res2, "\n")] = 0;
                    resolution = escapeCSV(resolution);
                    //fprintf(fp,"\"%s\",\"%s\",\"%f\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%d\"\n",conflict,groupId,jw2,regex2,replace2,resolution,res2,jv1,jv2,jdec,jid);
                    fprintf(fp, "\"%s\",\"%s\",\"%f\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%d\",\"%s\"\n", conflict,
                            groupId, jw2, regex2, replace2, resolution, res2, jv2, jdec, jid, cluster);
                    free(conflict);
                    free(regex2);
                    free(replace2);
                    free(res2);
                    free(cluster);
                }
            } else {
                double jw1 = jaro_winkler_distance(resolution, res1);
                regex1 = escapeCSV(regex1);
                replace1 = escapeCSV(replace1);
                res1 = escapeCSV(res1);
                regex1[strcspn(regex1, "\n")] = 0;
                replace1[strcspn(replace1, "\n")] = 0;
                res1[strcspn(res1, "\n")] = 0;
                resolution = escapeCSV(resolution);

                fprintf(fp, "\"%s\",\"%s\",\"%f\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%d\",\"%s\"\n", conflict,
                        groupId, jw1, regex1, replace1, resolution, res1, jv2, jdec, jid, cluster);
                free(conflict);
                free(regex1);
                free(replace1);
                free(res1);
                free(cluster);
            }
            fclose(fp);
            json_object_put(cluster_object);
            unlink(file_names[STRING_REPLACE]);
        } else {
            printf("Exit: regex replace jar end with error %d\n", status);
        }
    }

    printf("Exit: regex_replace_suggestion\n");
}

static void init_performance_file() {
    FILE *performance_log = fopen(file_names[PERFORMANCE], "w");
    if (performance_log == NULL)
        exit(EXIT_FAILURE);
    fprintf(performance_log, "\"%s\",\"%s\",\"%s\"\n", "Cluster", "Cluster Size", "Execution time [s]");
    fclose(performance_log);
}

static void init_similarity_stats_file() {
    printf("Statistics file \"%s\" :\n", file_names[CLUSTER_STATISTICS]);
    FILE *performance_log = fopen(file_names[CLUSTER_STATISTICS], "w");
    if (performance_log == NULL)
        exit(EXIT_FAILURE);
    fprintf(performance_log, "\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"\n", "Cluster", "ID", " AVG Intrasimilarity", "AVG Longest Distance","AVG Conflict","AVG Resolution","Longest Conflict","Longest Resolution", "Similarity Conflict", "Similarity Resolution");
    fclose(performance_log);
}

static char *get_property_value(char *line) {
    char *pch;
    char *save_ptr1;

    printf("Splitting string \"%s\" into tokens:\n", line);
    pch = strtok_r(line, "=", &save_ptr1);
    printf("%s\n", pch);
    pch = strtok_r(NULL, "=", &save_ptr1);
    printf("%s\n", pch);

    return pch;
}

static int init_properties() {

    FILE *fp;
    char *line = NULL;
    size_t len = 0;

    fp = fopen(CONFIG_FILE_PATH, "r");

    if (fp != NULL && getline(&line, &len, fp)) {
        line = get_property_value(line);
        workdir_path = malloc(strlen(line));
        strncpy(workdir_path, line, strlen(line) - 1);
        fclose(fp);

        file_names[CONFLICT_INDEX] = build_filename(CONFLICT_INDEX_FILENAME);
        file_names[CONFLICT_INDEX_RECLUSTER] = build_filename(CONFLICT_INDEX_RECLUSTER_FILENAME);
        file_names[STRING_REPLACE] = build_filename(STRING_REPLACE_FILENAME);
        file_names[STRING_REPLACE_RESULT] = build_filename(STRING_REPLACE_RESULT_FILENAME);
        file_names[PERFORMANCE] = build_filename(PERFORMANCE_FILENAME);
	file_names[CLUSTER_STATISTICS] = build_filename(CLUSTER_STATISTICS_FILENAME);

        return 0;
    }

    fclose(fp);
    return 1;
}

int main(int argc, char *argv[]) {
    printf("starting...\n");

    if (init_properties()) {
        printf("Something went wrong when loading properties of %s, exit...", CONFIG_FILE_PATH);
        return 0;
    }
    init_performance_file();

    if (argc == 1) {
        printf("No dataset file has been provided\n");
        return 0;
    }
    if (argc >= 2) {
        printf("Dataset: %s\n", argv[1]);
    }


    struct json_object *file_json = json_object_from_file(argv[1]);


    printf("file read...\n");
    if (!file_json) { // if file is empty
        printf("The file does not exist, it is empty or is not in a valid Json format\n");
        return 0;
    }
    struct json_object *obj;
    char *jconf = NULL;
    char *jresol = NULL;
    char *jv2 = NULL;
    char *jv1 = NULL;
    char *jdec = NULL;
    int jid = 0;
    int arraylen;

    cluster_population = 1;
    printf("processing...");
    json_object_object_foreach(file_json, key, val) {
        obj = NULL;
        arraylen = json_object_array_length(val);
        printf("arraylen: %d\n", arraylen);
        //for (int i = 0; i < arraylen; i++) {
        for (int i = 0; i < arraylen; i++) {
            printf("i = %d\n", i + 1);
            obj = json_object_array_get_idx(val, i);
            jid = json_object_get_int(json_object_object_get(obj, "id"));
	        conf_ID = jid;
            jconf = (char *) json_object_get_string(json_object_object_get(obj, "conflict"));
            jresol = (char *) json_object_get_string(json_object_object_get(obj, "resolution"));
            jv2 = (char *) json_object_get_string(json_object_object_get(obj, "v2"));
            jv1 = (char *) json_object_get_string(json_object_object_get(obj, "v1"));
            jdec = (char *) json_object_get_string(json_object_object_get(obj, "devdecision"));

            printf("jid: %d\n", jid);
            printf("jconf: %s\n", jconf);
            printf("jresol: %s\n", jresol);

            regex_replace_suggestion(jconf, jresol, jid, jv2, jdec);
            write_json_conflict_index(jconf, jresol, i + 1);
        }
    }
    json_object_put(file_json);

    //free file name strings
    for(int i = 0; i < 5; i++)
        free(file_names[i]);

    return 0;
}
