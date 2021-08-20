#include "cache.h"
#include "config.h"
#include "lockfile.h"
#include "string-list.h"
#include "rerere.h"
#include "xdiff-interface.h"
#include "dir.h"
#include "resolve-undo.h"
#include "ll-merge.h"
#include "attr.h"
#include "pathspec.h"
#include "object-store.h"
#include "sha1-lookup.h"
#include "json.h"

#define RESOLVED 0
#define PUNTED 1
#define THREE_STAGED 2
#define SCALING_FACTOR 0.1
#define similarity_th 0.80

void *RERERE_RESOLVED = &RERERE_RESOLVED;

/* if rerere_enabled == -1, fall back to detection of .git/rr-cache */
static int rerere_enabled = -1;

/* automatically update cleanly resolved paths to the index */
static int rerere_autoupdate;

static int rerere_dir_nr;
static int rerere_dir_alloc;

struct string_list groupId_list = STRING_LIST_INIT_NODUP; // list of json groupId that has been changed

#define RR_HAS_POSTIMAGE 1
#define RR_HAS_PREIMAGE 2
static struct rerere_dir {
    unsigned char hash[GIT_MAX_HEXSZ];
    int status_alloc, status_nr;
    unsigned char *status;
} **rerere_dir;

struct list {
    char conflict[2000];
    char resolution[2000];
    struct list *next;
};

static void free_rerere_dirs(void)
{
    int i;
    for (i = 0; i < rerere_dir_nr; i++) {
        free(rerere_dir[i]->status);
        free(rerere_dir[i]);
    }
    FREE_AND_NULL(rerere_dir);
    rerere_dir_nr = rerere_dir_alloc = 0;
}

static void free_rerere_id(struct string_list_item *item)
{
    free(item->util);
}

static const char *rerere_id_hex(const struct rerere_id *id)
{
    return sha1_to_hex(id->collection->hash);
}

static void fit_variant(struct rerere_dir *rr_dir, int variant)
{
    variant++;
    ALLOC_GROW(rr_dir->status, variant, rr_dir->status_alloc);
    if (rr_dir->status_nr < variant) {
        memset(rr_dir->status + rr_dir->status_nr,
               '\0', variant - rr_dir->status_nr);
        rr_dir->status_nr = variant;
    }
}

static void assign_variant(struct rerere_id *id)
{
    int variant;
    struct rerere_dir *rr_dir = id->collection;

    variant = id->variant;
    if (variant < 0) {
        for (variant = 0; variant < rr_dir->status_nr; variant++)
            if (!rr_dir->status[variant])
                break;
    }
    fit_variant(rr_dir, variant);
    id->variant = variant;
}

const char *rerere_path(const struct rerere_id *id, const char *file)
{
    if (!file)
        return git_path("rr-cache/%s", rerere_id_hex(id));

    if (id->variant <= 0)
        return git_path("rr-cache/%s/%s", rerere_id_hex(id), file);

    return git_path("rr-cache/%s/%s.%d",
                    rerere_id_hex(id), file, id->variant);
}

static int is_rr_file(const char *name, const char *filename, int *variant)
{
    const char *suffix;
    char *ep;

    if (!strcmp(name, filename)) {
        *variant = 0;
        return 1;
    }
    if (!skip_prefix(name, filename, &suffix) || *suffix != '.')
        return 0;

    errno = 0;
    *variant = strtol(suffix + 1, &ep, 10);
    if (errno || *ep)
        return 0;
    return 1;
}

static void scan_rerere_dir(struct rerere_dir *rr_dir)
{
    struct dirent *de;
    DIR *dir = opendir(git_path("rr-cache/%s", sha1_to_hex(rr_dir->hash)));

    if (!dir)
        return;
    while ((de = readdir(dir)) != NULL) {
        int variant;

        if (is_rr_file(de->d_name, "postimage", &variant)) {
            fit_variant(rr_dir, variant);
            rr_dir->status[variant] |= RR_HAS_POSTIMAGE;
        } else if (is_rr_file(de->d_name, "preimage", &variant)) {
            fit_variant(rr_dir, variant);
            rr_dir->status[variant] |= RR_HAS_PREIMAGE;
        }
    }
    closedir(dir);
}

static const unsigned char *rerere_dir_hash(size_t i, void *table)
{
    struct rerere_dir **rr_dir = table;
    return rr_dir[i]->hash;
}

static struct rerere_dir *find_rerere_dir(const char *hex)
{
    unsigned char hash[GIT_MAX_RAWSZ];
    struct rerere_dir *rr_dir;
    int pos;

    if (get_sha1_hex(hex, hash))
        return NULL; /* BUG */
    pos = sha1_pos(hash, rerere_dir, rerere_dir_nr, rerere_dir_hash);
    if (pos < 0) {
        rr_dir = xmalloc(sizeof(*rr_dir));
        hashcpy(rr_dir->hash, hash);
        rr_dir->status = NULL;
        rr_dir->status_nr = 0;
        rr_dir->status_alloc = 0;
        pos = -1 - pos;

        /* Make sure the array is big enough ... */
        ALLOC_GROW(rerere_dir, rerere_dir_nr + 1, rerere_dir_alloc);
        /* ... and add it in. */
        rerere_dir_nr++;
        MOVE_ARRAY(rerere_dir + pos + 1, rerere_dir + pos,
                   rerere_dir_nr - pos - 1);
        rerere_dir[pos] = rr_dir;
        scan_rerere_dir(rr_dir);
    }
    return rerere_dir[pos];
}

static int has_rerere_resolution(const struct rerere_id *id)
{
    const int both = RR_HAS_POSTIMAGE|RR_HAS_PREIMAGE;
    int variant = id->variant;

    if (variant < 0)
        return 0;
    return ((id->collection->status[variant] & both) == both);
}

static struct rerere_id *new_rerere_id_hex(char *hex)
{
    struct rerere_id *id = xmalloc(sizeof(*id));
    id->collection = find_rerere_dir(hex);
    id->variant = -1; /* not known yet */
    return id;
}

static struct rerere_id *new_rerere_id(unsigned char *sha1)
{
    //fprintf_ln(stderr, _("!!!!!!! new_rerere_id sha1_to_hex() function: '%s"),sha1_to_hex(sha1));
    return new_rerere_id_hex(sha1_to_hex(sha1));
}

/*
 * $GIT_DIR/MERGE_RR file is a collection of records, each of which is
 * "conflict ID", a HT and pathname, terminated with a NUL, and is
 * used to keep track of the set of paths that "rerere" may need to
 * work on (i.e. what is left by the previous invocation of "git
 * rerere" during the current conflict resolution session).
 */
static void read_rr(struct repository *r, struct string_list *rr)
{
    //fprintf_ln(stderr, _("LOG_ENTER: read_rr function"));
    struct strbuf buf = STRBUF_INIT;
    FILE *in = fopen_or_warn(git_path_merge_rr(r), "r");

    if (!in)
        return;
    while (!strbuf_getwholeline(&buf, in, '\0')) {
        char *path;
        unsigned char hash[GIT_MAX_RAWSZ];
        struct rerere_id *id;
        int variant;
        const unsigned hexsz = the_hash_algo->hexsz;

        /* There has to be the hash, tab, path and then NUL */
        if (buf.len < hexsz + 2 || get_sha1_hex(buf.buf, hash))
            die(_("corrupt MERGE_RR"));

        if (buf.buf[hexsz] != '.') {
            //fprintf_ln(stderr, _("read_rr: variant = 0"));
            variant = 0;
            path = buf.buf + hexsz;
        } else {
            errno = 0;
            variant = strtol(buf.buf + hexsz + 1, &path, 10);
            //fprintf_ln(stderr, _("read_rr: variant = '%d'"),variant);
            if (errno)
                die(_("corrupt MERGE_RR"));
        }
        if (*(path++) != '\t')
            die(_("corrupt MERGE_RR"));
        buf.buf[hexsz] = '\0';
        id = new_rerere_id_hex(buf.buf);
        id->variant = variant;
        string_list_insert(rr, path)->util = id;
    }
    strbuf_release(&buf);
    fclose(in);
    //fprintf_ln(stderr, _("LOG_EXIT: read_rr function"));
}

static struct lock_file write_lock;

static int write_rr(struct string_list *rr, int out_fd)
{
    int i;
    for (i = 0; i < rr->nr; i++) {
        struct strbuf buf = STRBUF_INIT;
        struct rerere_id *id;

        assert(rr->items[i].util != RERERE_RESOLVED);

        id = rr->items[i].util;
        if (!id)
            continue;
        assert(id->variant >= 0);
        if (0 < id->variant)
            strbuf_addf(&buf, "%s.%d\t%s%c",
                        rerere_id_hex(id), id->variant,
                        rr->items[i].string, 0);
        else
            strbuf_addf(&buf, "%s\t%s%c",
                        rerere_id_hex(id),
                        rr->items[i].string, 0);

        if (write_in_full(out_fd, buf.buf, buf.len) < 0)
            die(_("unable to write rerere record"));

        strbuf_release(&buf);
    }
    if (commit_lock_file(&write_lock) != 0)
        die(_("unable to write rerere record"));
    return 0;
}

/*
 * "rerere" interacts with conflicted file contents using this I/O
 * abstraction.  It reads a conflicted contents from one place via
 * "getline()" method, and optionally can write it out after
 * normalizing the conflicted hunks to the "output".  Subclasses of
 * rerere_io embed this structure at the beginning of their own
 * rerere_io object.
 */
struct rerere_io {
    int (*getline)(struct strbuf *, struct rerere_io *);
    FILE *output;
    int wrerror;
    /* some more stuff */
};

static void ferr_write(const void *p, size_t count, FILE *fp, int *err)
{
    if (!count || *err)
        return;
    if (fwrite(p, count, 1, fp) != 1)
        *err = errno;
}

static inline void ferr_puts(const char *s, FILE *fp, int *err)
{
    ferr_write(s, strlen(s), fp, err);
}

static void rerere_io_putstr(const char *str, struct rerere_io *io)
{
    if (io->output)
        ferr_puts(str, io->output, &io->wrerror);
}

static void rerere_io_putmem(const char *mem, size_t sz, struct rerere_io *io)
{
    if (io->output)
        ferr_write(mem, sz, io->output, &io->wrerror);
}

/*
 * Subclass of rerere_io that reads from an on-disk file
 */
struct rerere_io_file {
    struct rerere_io io;
    FILE *input;
};

/*
 * ... and its getline() method implementation
 */
static int rerere_file_getline(struct strbuf *sb, struct rerere_io *io_)
{
    struct rerere_io_file *io = (struct rerere_io_file *)io_;
    return strbuf_getwholeline(sb, io->input, '\n');
}

/*
 * Require the exact number of conflict marker letters, no more, no
 * less, followed by SP or any whitespace
 * (including LF).
 */
static int is_cmarker(char *buf, int marker_char, int marker_size)
{
    int want_sp;

    /*
     * The beginning of our version and the end of their version
     * always are labeled like "<<<<< ours" or ">>>>> theirs",
     * hence we set want_sp for them.  Note that the version from
     * the common ancestor in diff3-style output is not always
     * labelled (e.g. "||||| common" is often seen but "|||||"
     * alone is also valid), so we do not set want_sp.
     */
    want_sp = (marker_char == '<') || (marker_char == '>');

    while (marker_size--)
        if (*buf++ != marker_char)
            return 0;
    if (want_sp && *buf != ' ')
        return 0;
    return isspace(*buf);
}

static void rerere_strbuf_putconflict(struct strbuf *buf, int ch, size_t size)
{
    strbuf_addchars(buf, ch, size);
    strbuf_addch(buf, '\n');
}

static char* remove_spaces(char *s)
{
    struct strbuf temp = STRBUF_INIT;

    while(*s++) {
        if (*s != ' ' && *s != '\n' && *s != '\0') {
            strbuf_addch(&temp,*s);
        }
    }
    char *str = temp.buf;
    if (strlen(str) == 0) {
        str = NULL;
    }

    strbuf_release(&temp);
    return str;
}

static int max(int x, int y) {
    return x > y ? x : y;
}

static int min(int x, int y) {
    return x < y ? x : y;
}

static double jaro_winkler_distance(const char *s, const char *a) {
    int i, j, l;
    int m = 0, t = 0;
    int sl = strlen(s);
    int al = strlen(a);
    int sflags[sl], aflags[al];
    int range = max(0, max(sl, al) / 2 - 1);
    double dw;

    if (!sl || !al)
        return 0.0;

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
    dw = (((double)m / sl) + ((double)m / al) + ((double)(m - t) / m)) / 3.0;

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


static size_t levenshtein_n(const char *a, const size_t length, const char *b, const size_t bLength) {
    // Shortcut optimizations / degenerate cases.
    if (a == b) {
        return 0;
    }

    if (length == 0) {
        return bLength;
    }

    if (bLength == 0) {
        return length;
    }

    size_t *cache = calloc(length, sizeof(size_t));
    size_t index = 0;
    size_t bIndex = 0;
    size_t distance;
    size_t bDistance;
    size_t result;
    char code;

    // initialize the vector.
    while (index < length) {
        cache[index] = index + 1;
        index++;
    }

    // Loop.
    while (bIndex < bLength) {
        code = b[bIndex];
        result = distance = bIndex++;
        index = SIZE_MAX;

        while (++index < length) {
            bDistance = code == a[index] ? distance : distance + 1;
            distance = cache[index];

            cache[index] = result = distance > result
                                    ? bDistance > result
                                      ? result + 1
                                      : bDistance
                                    : bDistance > distance
                                      ? distance + 1
                                      : bDistance;
        }
    }

    free(cache);

    return result;
}

static size_t levenshtein(const char *a, const char *b) {
    const size_t length = strlen(a);
    const size_t bLength = strlen(b);

    size_t leve = levenshtein_n(a, length, b, bLength);
    //fprintf_ln(stderr, _("leve: %zu\n\n"),leve);
    return leve;
}

/*
 * return null if conflict and resolution are already present in json file
 * otherwise return id of the group with jaro-winkler similarity greater than 0.90 or
 * return a new id
 * resolution can be null if you want only group id
 */
static const char* get_conflict_json_id(char* conflict,char* resolution)
{
    //fprintf_ln(stderr, _("LOG_ENTER: get_conflict_json_id function"));

    struct json_object *file_json = json_object_from_file(".git/rr-cache/conflict_index.json");
    if (!file_json) { // if file is empty
        if (!resolution) { //resolution is NULL
            return  NULL;
        }
        return "1";
    }

    double jaroW = 0 ;
    //size_t leve = 0 ;
    const char* groupId = NULL;
    double max_sim = similarity_th;
    char* idCount;

    fprintf_ln(stderr, _("\nconflict: %s"),conflict);
    //fprintf_ln(stderr, _("resolution: %s\n"),resolution);

    struct json_object *obj;
    const char* jconf;
    const char* jresol;
    int arraylen;
    double total_similarity = 0;
    json_object_object_foreach(file_json,key,val){
        obj = NULL;
        arraylen = json_object_array_length(val);
        idCount = key;
        total_similarity = 0;
        for (int i = 0; i < arraylen; i++) {
            obj = json_object_array_get_idx(val, i);
            jconf = json_object_get_string(json_object_object_get(obj, "conflict"));
            jresol = json_object_get_string(json_object_object_get(obj, "resolution"));

            if (resolution) {
                if (strcmp(conflict, jconf) == 0 && strcmp(resolution, jresol) == 0) {
                    //fprintf_ln(stderr,
                    //          _("LOG_EXIT: get_conflict_json_id : conflict and resolution already present in json file\n"));
                    json_object_put(file_json);
                    return NULL;
                }
            }

            jaroW = jaro_winkler_distance(conflict,jconf);
            total_similarity += jaroW;
        }
        double avg = total_similarity/arraylen;
        if (avg >= max_sim) {
            max_sim = avg;
            groupId = key;
        }
    }

    if (!groupId && resolution) { //if group == null and resolution != null
        //create new group id
        groupId = json_object_to_json_string(json_object_new_int(atoi(idCount)+1));
    }

    fprintf_ln(stderr, _("groupID:  %s"),groupId);
    //fprintf_ln(stderr, _("LOG_EXIT: get_conflict_json_id : groupID %s"),groupId);
    json_object_put(file_json);
    return groupId;
}
/*
 * return 1 if it is multiline otherwise return 0
 */
static int is_multiline_string(char* string)
{
    char *line = strdup(string);
    strtok(line,"\n");
    char * secondline= strtok(NULL, "\n");
    if(secondline != NULL){
        fprintf_ln(stderr, _("Multiline String is not accepted"));
        free(line);
        return 1;
    }
    free(line);
    return 0;
}

static int write_json_object(struct json_object* file_object, char* file_name, const char* group_id, char* conflict, char* resolution){

    struct json_object *object = json_object_new_object();
    struct json_object *jarray = json_object_new_array();

    jarray = json_object_object_get(file_object, group_id);

    if(!jarray)
        jarray = json_object_new_array();

    if (json_object_array_length(jarray)) { // get object id1 if exists
        //add new line to object id1
        json_object_object_add(object, "conflict", json_object_new_string(conflict));
        json_object_object_add(object, "resolution", json_object_new_string(resolution));
        json_object_array_add(jarray,object);
    } else { // if id1 not exists
        json_object_object_add(object, "conflict", json_object_new_string(conflict));
        json_object_object_add(object, "resolution", json_object_new_string(resolution));
        json_object_array_add(jarray,object);
        json_object_object_add(file_object,group_id,jarray);
    }

    FILE *fp = fopen(file_name,"w");
    if (!fp)
        return 0;
    //update or add groupid to file
    fprintf(fp,"%s", json_object_to_json_string_ext(file_object,2));
    fclose(fp);
    free(object);
    free(jarray);
    return 1;
}

static int write_json_conflict_index(char* conflict, char* resolution)
{
    //fprintf_ln(stderr, _("LOG_ENTER: write_json_conflict_index function"));

    if (strlen(remove_spaces(conflict)) == 0 || strlen(remove_spaces(resolution)) == 0)
        return 0;

    struct json_object *file_json = json_object_from_file(".git/rr-cache/conflict_index.json");

    if (!file_json) // if file is empty
        file_json = json_object_new_object();

    const char* group_id = get_conflict_json_id(conflict,resolution);

    if (!group_id) {
        json_object_put(file_json);
        return 0;
    }

    if(!write_json_object(file_json,".git/rr-cache/conflict_index.json",group_id,conflict,resolution)){ //if return 0
        json_object_put(file_json);
        return 0;
    }

    struct json_object *conflict_list = json_object_from_file(".git/rr-cache/conflict_list.json");

    if (!conflict_list) // if file is empty
        conflict_list = json_object_new_object();

    // write conflict list file
    if(!write_json_object(conflict_list,".git/rr-cache/conflict_list.json","conflicts_list",conflict,resolution)){ //if return 0
        json_object_put(file_json);
        return 0;
    }

    //save cluster id for jar file
    string_list_insert(&groupId_list,group_id);

    //free(file_json);
    json_object_put(file_json);
    free(conflict_list);
    //fprintf_ln(stderr, _("LOG_EXIT: write_json_conflict_index function"));
    return 1;
}

static char* get_file_hash (const char *path)
{
    //fprintf_ln(stderr, _("LOG_ENTER: get_hash_file function"));
    struct strbuf buf = STRBUF_INIT;
    struct rerere_io_file io;
    memset(&io, 0, sizeof(io));
    io.io.getline = rerere_file_getline;
    io.input = fopen(".git/rr-cache/hash_index", "r");

    io.io.wrerror = 0;
    if (io.input) {
        while (!io.io.getline(&buf, &io.io)) {

            if (strstr(buf.buf, path) != NULL) {
                char *hash = strchr(buf.buf, ';');
                hash++;
                size_t ln = strlen(hash) - 1;
                if (*hash && hash[ln] == '\n')
                    hash[ln] = '\0';

                fclose(io.input);
                return hash;
            }
        }
        fclose(io.input);
    }
    //fprintf_ln(stderr, _("LOG_EXIT: get_hash_file function"));
    return 0;
}

/*
 * read conflict file index
 */
static void read_conflict_index(struct list **c_list)
{
    //fprintf_ln(stderr, _("read_conflict_index: ENTER"));
    struct index {
        char conflict[2000];
        char resolution[2000];
    } object;

    struct list *conflict_list = (*c_list);

    FILE *conflict_index = fopen(".git/rr-cache/conflict_index", "r");

    if (conflict_index) {
        while (fread(&object, sizeof(struct index), 1, conflict_index)) {
            // printf("write_conflict_index: CONFLICT %s ", object2.conflict);
            //printf("write_conflict_index: RESOLUTION %s", object2.resolution);
            //printf("\n");

            if (conflict_list == NULL){
                conflict_list = malloc(sizeof(struct list));
                memcpy(conflict_list->conflict, object.conflict, strlen(object.conflict)+1);
                memcpy(conflict_list->resolution, object.resolution, strlen(object.resolution)+1);
                conflict_list->next = NULL;
            } else{
                struct list *node = malloc(sizeof(struct list));
                memcpy(node->conflict, object.conflict, strlen(object.conflict)+1);
                memcpy(node->resolution, object.resolution, strlen(object.resolution)+1);
                node->next = conflict_list;
                conflict_list = node;
            }
        }
        fclose(conflict_index);
    } else {
        fprintf_ln(stderr, _("conflict_index_file does not exist"));
    }
    (*c_list) = conflict_list;
    //fprintf_ln(stderr, _("read_conflict_index: EXIT"));
}

static void write_conflict_index(char* conflict, char* resolution)
{
    struct index {
        char conflict[2000];
        char resolution[2000];
    };

    if (strlen(conflict) > 2000 || strlen(resolution) > 2000){
        fprintf_ln(stderr, _("write_conflict_index: STRING IS TOO LONG FOR INDEX FILE"));
        return;
    }

    FILE *conflict_index = fopen(".git/rr-cache/conflict_index", "a+");

    if (conflict_index) {
        struct index new_conflict = {"",""};
        memcpy(new_conflict.conflict, conflict, strlen(conflict));
        memcpy(new_conflict.resolution, resolution, strlen(resolution));
        fwrite(&new_conflict, sizeof(struct index), 1, conflict_index);
        fclose(conflict_index);
    }
}

static int handle_conflict(struct strbuf *out, struct rerere_io *io,
                           int marker_size, git_hash_ctx *ctx)
{
    enum {
        RR_SIDE_1 = 0, RR_SIDE_2, RR_ORIGINAL
    } hunk = RR_SIDE_1;
    struct strbuf one = STRBUF_INIT, two = STRBUF_INIT;
    struct strbuf buf = STRBUF_INIT, conflict = STRBUF_INIT;
    int has_conflicts = -1;

    while (!io->getline(&buf, io)) {
        if (is_cmarker(buf.buf, '<', marker_size)) {
            if (handle_conflict(&conflict, io, marker_size, NULL) < 0)
                break;
            if (hunk == RR_SIDE_1)
                strbuf_addbuf(&one, &conflict);
            else
                strbuf_addbuf(&two, &conflict);
            strbuf_release(&conflict);
        } else if (is_cmarker(buf.buf, '|', marker_size)) {
            if (hunk != RR_SIDE_1)
                break;
            hunk = RR_ORIGINAL;
        } else if (is_cmarker(buf.buf, '=', marker_size)) {
            if (hunk != RR_SIDE_1 && hunk != RR_ORIGINAL)
                break;
            hunk = RR_SIDE_2;
        } else if (is_cmarker(buf.buf, '>', marker_size)) {
            if (hunk != RR_SIDE_2)
                break;
            // < 0 : one < two
            // = 0 : one = two
            // > 0 : one > two
            if (strbuf_cmp(&one, &two) > 0) // if one > two then swap
                strbuf_swap(&one, &two);
            has_conflicts = 1;
            //preparing output out
            rerere_strbuf_putconflict(out, '<', marker_size); //add < marker_size time in out
            strbuf_addbuf(out, &one); // add content of one in out
            rerere_strbuf_putconflict(out, '=', marker_size); // add = marker_size time in out
            strbuf_addbuf(out, &two); // add content of two in out
            rerere_strbuf_putconflict(out, '>', marker_size); // add > marker_size time in out
            if (ctx) {
                the_hash_algo->update_fn(ctx, one.buf ?
                                              one.buf : "",
                                         one.len + 1);
                the_hash_algo->update_fn(ctx, two.buf ?
                                              two.buf : "",
                                         two.len + 1);
            }
            break;
        } else if (hunk == RR_SIDE_1)
            strbuf_addbuf(&one, &buf);
        else if (hunk == RR_ORIGINAL)
            ; /* discard */
        else if (hunk == RR_SIDE_2)
            strbuf_addbuf(&two, &buf);
    }
    strbuf_release(&one);
    strbuf_release(&two);
    strbuf_release(&buf);

    return has_conflicts;
}

/*
 * Read contents a file with conflicts, normalize the conflicts
 * by (1) discarding the common ancestor version in diff3-style,
 * (2) reordering our side and their side so that whichever sorts
 * alphabetically earlier comes before the other one, while
 * computing the "conflict ID", which is just an SHA-1 hash of
 * one side of the conflict, NUL, the other side of the conflict,
 * and NUL concatenated together.
 *
 * Return 1 if conflict hunks are found, 0 if there are no conflict
 * hunks and -1 if an error occured.
 */
static int handle_path(unsigned char *hash, struct rerere_io *io, int marker_size)
{
    git_hash_ctx ctx;
    struct strbuf buf = STRBUF_INIT, out = STRBUF_INIT;
    int has_conflicts = 0;
    if (hash)
        the_hash_algo->init_fn(&ctx);

    while (!io->getline(&buf, io)) {
        if (is_cmarker(buf.buf, '<', marker_size)) {
            has_conflicts = handle_conflict(&out, io, marker_size,
                                            hash ? &ctx : NULL);
            if (has_conflicts < 0)
                break;
            rerere_io_putmem(out.buf, out.len, io);
            strbuf_reset(&out);
        } else
            rerere_io_putstr(buf.buf, io);
    }
    strbuf_release(&buf);
    strbuf_release(&out);

    if (hash)
        the_hash_algo->final_fn(hash, &ctx);

    return has_conflicts;
}

/*
 * Require the exact number of conflict marker letters, no more, no
 * less, followed by SP or any whitespace
 * (including LF).
 */
static int my_cmarker(char *buf, int marker_char, int marker_size)
{
    while (marker_size--) {
        if (*buf++ != marker_char) {
            return 0;
        }
    }
    return 1;
}

static void regex_repalce_suggestion(char *conflict)
{
    //fprintf_ln(stderr, _("LOG_ENTER: regex_repalce_suggestion"));
/*
    struct json_object *regex_json = json_object_from_file("/Users/manan/CLionProjects/git/search-and-replace/regex_replace_index.json");
    if (!regex_json) {
        fprintf_ln(stderr, _("LOG_EXIT: regex_repalce_suggestion: regex_replace_index does not exists "));
        return;
    }
*/
    const char *groupId = get_conflict_json_id(conflict,NULL);

    if (!groupId)
        return;

    //fprintf_ln(stderr, _("\nConflict: %s"),conflict);
    pid_t pid = fork();
    if (pid == 0) { // child process
        /* open /dev/null for writing */
        int fd = open(".git/rr-cache/string_replace.txt", O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
        dup2(fd, 1);    /* make stdout a copy of fd (> /dev/null) */
        //dup2(fd, 2);    /* ...and same with stderr */
        close(fd);
        execl("/usr/bin/java", "/usr/bin/java", "-jar", "/Users/manan/CLionProjects/git/search-and-replace/RegexReplacement.jar","/Users/manan/CLionProjects/git/search-and-replace/",groupId,conflict,(char*)0);
    } else { //parent process
        int status;
        (void)waitpid(pid, &status, 0);
        //printf("\nJar Process terminate with code: %d\n",status);

        if (!status) {
            struct strbuf buf1 = STRBUF_INIT, buf2 = STRBUF_INIT, buf3 = STRBUF_INIT;
            struct rerere_io_file io;
            memset(&io, 0, sizeof(io));
            io.io.getline = rerere_file_getline;
            io.input = fopen(".git/rr-cache/string_replace.txt", "r");

            io.io.wrerror = 0;
            if (io.input) {
                char *regex1=NULL, *regex2=NULL, *replace1=NULL, *replace2=NULL, *res1=NULL, *res2=NULL;
                // read first resolution
                if (!io.io.getline(&buf1, &io.io) && !io.io.getline(&buf2, &io.io) && !io.io.getline(&buf3, &io.io)) {
                    regex1 = buf1.buf;
                    replace1 = buf2.buf;
                    res1 = buf3.buf;
                }
                // read second resolution
                if (!io.io.getline(&buf1, &io.io) && !io.io.getline(&buf2, &io.io) && !io.io.getline(&buf3, &io.io)) {
                    regex2 = buf1.buf;
                    replace2 = buf2.buf;
                    res2 = buf3.buf;
                }

                if(res1 && res2){
                    double jw1 = jaro_winkler_distance(conflict,res1);
                    double jw2 = jaro_winkler_distance(conflict,res2);
                    FILE *fp = fopen(".git/rr-cache/regex_replace_result.txt","a+");
                    fprintf(fp, "\nconflict: %s",conflict);
                    fprintf(fp, "groupID: %s",groupId);
                    if(jw1 >= jw2){
                        fprintf(fp, "regex: %s",regex1);
                        fprintf(fp, "replacement: %s",replace1);
                        fprintf(fp, "regex & replacement: %s",res1);
                    }else{
                        fprintf(fp, "regex: %s",regex2);
                        fprintf(fp, "replacement: %s",replace2);
                        fprintf(fp, "regex & replacement: %s",res2);
                    }
                    fclose(fp);
                }else{
                    FILE *fp = fopen(".git/rr-cache/regex_replace_result.txt","a+");
                    fprintf(fp, "conflict: %s",conflict);
                    fprintf(fp, "groupID: %s",groupId);
                    fprintf(fp, "regex: %s",regex1);
                    fprintf(fp, "replacement: %s",replace1);
                    fprintf(fp, "regex & replacement: %s",res1);
                }
                fclose(io.input);
                unlink_or_warn(".git/rr-cache/string_replace.txt");
            }else{
                FILE *fp = fopen(".git/rr-cache/regex_replace_result.txt","a+");
                fprintf(fp, "conflict: %s",conflict);
                fprintf(fp, "groupID: %s",groupId);
                fprintf(fp, "Regex not apply: %s",conflict);
            }
            strbuf_release(&buf1);
            strbuf_release(&buf2);
            strbuf_release(&buf3);
        }
    }
    //free(regex_json);
    //free(jobject);
    //fprintf_ln(stderr, _("LOG_EXIT: regex_repalce_suggestion"));
}

static void conflict_suggestion(char *conflict)
{
    //fprintf_ln(stderr, _("conflict_suggestion: ENTER"));
    //struct list conflict_list = read_conflict_index();
    struct list *conflict_list = NULL;
    read_conflict_index(&conflict_list);

    if (!conflict_list) {
        //fprintf_ln(stderr, _("conflict_suggestion: EXIT"));
        return;
    }
    struct list *current = conflict_list;
    double jaroW = 0 ;
    size_t leve = 0 ;

    double max_similarity = 0;
    char* solution;
    char* file_conflict;
    //fprintf_ln(stderr, _("conflict_suggestion: conflict: %s\n"),conflict);
    while (current != NULL) {
        jaroW = jaro_winkler_distance(conflict,current->conflict);
        leve = levenshtein(conflict,current->conflict);

        if (max_similarity < jaroW) {
            max_similarity = jaroW;
            solution = current->resolution;
            file_conflict = current->conflict;
        }
        current = current->next;
    }
    fprintf_ln(stderr, _("file_conflict:%s\nresolution:%s\nsimilarity: %lf\n"),file_conflict,solution,max_similarity);

    free(current);
    free(conflict_list);
    //fprintf_ln(stderr, _("conflict_suggestion: EXIT"));
}


static int control_whitespace_diff(char *cur_s, char *pre_s,struct strbuf *out_buf)
{
    //fprintf_ln(stderr, _("control_whitespace_diff: ENTER"));

    if (strcmp(remove_spaces(cur_s),remove_spaces(pre_s)) != 0) {
        return 0; // if without whitespaces strings are different
    }

    char * cur_str = malloc(strlen(cur_s)+1);
    memcpy(cur_str, cur_s, strlen(cur_s));

    char * pre_str = malloc(strlen(pre_s)+1);
    memcpy(pre_str, pre_s, strlen(pre_s));

    cur_str[strlen(cur_s)] = 0;
    pre_str[strlen(pre_s)] = 0;

    int count = 1;

    while(*cur_str++){
        if (*cur_str == pre_str[count]) {
            count += 1;
        }else if (*cur_str == ' ') {
            continue;
        } else
            return 0;
    }

    //only space difference add string to output buffer
    strbuf_addstr(out_buf, cur_s);

    free(cur_str);
    free(pre_str);
    //fprintf_ln(stderr, _("control_whitespace_diff: EXIT"));
    return 1;
}

static char* get_html_comment(char *buf)
{
    //fprintf_ln(stderr, _("LOG_ENTER: get_html_comment"));

    const char *PATTERN1 = "<!--";
    const char *PATTERN2 = " <!--";
    const char *PATTERN3 = "-->";
    const char *PATTERN4 = "--> ";

    char *comment = NULL;

    if ((strstr(buf, "<!--") != NULL) && (strstr(buf, "-->") != NULL)) {
        //if ((strstr(pre_buf->buf, "<!--") == NULL) && (strstr(pre_buf->buf, "-->") == NULL)) {

        char *start, *end;

        start = strstr(buf, PATTERN2); //comment start with initial space
        if (!start)
            start = strstr(buf, PATTERN1); //comment start without initial space

        if (start) {
            end = strstr(start, PATTERN4); //comment end with space
            if (!end)
                end = strstr(start, PATTERN3); //comment end without space

            if (end) {
                end += strlen(PATTERN3);
                comment = (char *) malloc(end - start + 1);
                memcpy(comment, start, end - start);
                comment[end - start] = '\0';

                fprintf_ln(stderr, _("COMMENT: %s"), comment);
                fprintf_ln(stderr, _("LOG_EXIT: get_html_comment"));
                return comment;
            }
        } else {
            fprintf_ln(stderr, _("No Comment Pattern Found"));
            fprintf_ln(stderr, _("LOG_EXIT: get_html_comment"));
            return comment;
        }
        //}
    }
    //fprintf_ln(stderr, _("No HTML Comment Found"));
    //fprintf_ln(stderr, _("LOG_EXIT: get_html_comment"));
    return comment;
}


static int control_html_comment(struct strbuf *cur_buf,struct strbuf *pre_buf,struct strbuf *out_buf)
{
    fprintf_ln(stderr, _("LOG_ENTER: control_html_comment"));

    char * comment;
    if ((comment = get_html_comment(cur_buf->buf)) != NULL){

        if (get_html_comment(pre_buf->buf) != NULL) {
            return -1; // case preimage has already a html comment
        }

        if (strcmp(remove_spaces(comment),remove_spaces(cur_buf->buf)) == 0) {
            strbuf_addbuf(out_buf,cur_buf); //new line
            fprintf_ln(stderr, _("LOG_EXIT: control_html_comment: new line with comment"));
            return 0;
        }

        struct strbuf temp_buf = STRBUF_INIT;
        strbuf_addbuf(&temp_buf,pre_buf);

        strbuf_insert(&temp_buf,temp_buf.len,comment,(size_t) strlen(comment));

        if (strbuf_cmp(cur_buf,&temp_buf) == 0){
            //two buffers are equal after adding comment to preimage
            strbuf_addbuf(out_buf,cur_buf);
            strbuf_release(&temp_buf);
            return 1;
        } else if (strcmp(remove_spaces(cur_buf->buf),remove_spaces(temp_buf.buf)) == 0) {
            // two strings are equal after adding comment and removing all spaces
            strbuf_addbuf(out_buf,cur_buf);
            strbuf_release(&temp_buf);
            return 1;
        } else { // case <!-- <tr></> -->
            strbuf_reset(&temp_buf);
            strbuf_addstr(&temp_buf,remove_spaces(pre_buf->buf));
            strbuf_insert(&temp_buf,(size_t) 0,"<!--",(size_t) strlen("<!--"));
            strbuf_insert(&temp_buf,(size_t) temp_buf.len,"-->",(size_t) strlen("-->"));

            if (strcmp(remove_spaces(cur_buf->buf),temp_buf.buf) == 0) {
                // two strings are equal
                strbuf_addbuf(out_buf,cur_buf);
                strbuf_release(&temp_buf);
                return 1;
            }
        }
    }

    fprintf_ln(stderr, _("No Comment Found"));
    fprintf_ln(stderr, _("LOG_EXIT: control_html_comment"));
    return -1;
}

static int control_line_character(struct string_list *list_A,struct string_list *list_B, struct strbuf *out_buf)
{
    //fprintf_ln(stderr, _("LOG_ENTER: control_line_character"));

    struct strbuf buf_A = STRBUF_INIT, buf_B = STRBUF_INIT;

    int cur = 0, pre = 0, res = 0;

    //fprintf_ln(stderr, _("control_line_character: list_A size : %d"),list_A->nr);
    //fprintf_ln(stderr, _("control_line_character: list_B size : %d"),list_B->nr);

    while (cur < list_A->nr && pre < list_B->nr) {

        strbuf_addstr(&buf_A,list_A->items[cur].string);
        strbuf_addstr(&buf_B,list_B->items[pre].string);

        //fprintf_ln(stderr, _("cur_buf : %s"),buf_A.buf);
        //fprintf_ln(stderr, _("pre_buf : %s"),buf_B.buf);

        if (strbuf_cmp(&buf_A, &buf_B) == 0) {
            strbuf_addbuf(out_buf, &buf_A);
            fprintf_ln(stderr, _("UGUALIIIII"));
            cur += 1;
            pre += 1;
        } else if (remove_spaces(buf_A.buf) == NULL) {
            fprintf_ln(stderr, _("VUOTOOO"));
            strbuf_addbuf(out_buf,&buf_A);
            cur += 1;
        } else if ((res = control_html_comment(&buf_A, &buf_B, out_buf)) >= 0) {
            if (res == 0) //new line with comment
                cur += 1;

            if (res == 1) { // two strings are equal after adding comment(and removing all spaces)
                cur += 1;
                pre += 1;
            }
        } else if (control_whitespace_diff(buf_A.buf,buf_B.buf,out_buf)) {
            cur += 1;
            pre += 1;
        } else {
            fprintf_ln(stderr, _("TWO LINES ARE DIFFERENT."));
            //conflict_suggestion(buf_A.buf);
            regex_repalce_suggestion(buf_A.buf);
            strbuf_release(&buf_A);
            strbuf_release(&buf_B);
            fprintf_ln(stderr, _("LOG_EXIT: control_line_character"));
            return -1;
        }
        strbuf_reset(&buf_A);
        strbuf_reset(&buf_B);

        if (cur < list_A->nr && pre == list_B->nr) { // Assumption: list_A could be bigger than list_B
            pre -= 1;
        }

        //fprintf_ln(stderr, _("control_line_character: CUR : %d"),cur);
        //fprintf_ln(stderr, _("control_line_character: PRE : %d"),pre);
    }
    strbuf_release(&buf_A);
    strbuf_release(&buf_B);
    //fprintf_ln(stderr, _("LOG_EXIT: control_line_character"));
    return 1;
}

static int separate_conflict_area(struct rerere_io *io,struct strbuf *buf_A,struct strbuf *buf_B,int marker_size,
                                  struct string_list *list_A, struct string_list *list_B)
{
    //fprintf_ln(stderr, _("LOG_ENTER: separate_conflict_area"));
    struct strbuf buf = STRBUF_INIT;

    enum {
        RR_SIDE_1 = 0,  // = 0
        RR_SIDE_2,      // = 1
        RR_ORIGINAL     // = 2
    } hunk = RR_SIDE_1;

    while (!io->getline(&buf,io)) {
        if(my_cmarker(buf.buf,'<',marker_size)) {
            continue;
        } else if (my_cmarker(buf.buf,'|',marker_size)) {
            if (hunk != RR_SIDE_1)
                return -1;
            hunk = RR_ORIGINAL;
        } else if (my_cmarker(buf.buf, '=', marker_size)) {
            if (hunk != RR_SIDE_1 && hunk != RR_ORIGINAL)
                return -1;
            hunk = RR_SIDE_2;
        } else if (my_cmarker(buf.buf, '>', marker_size)) {
            if (hunk != RR_SIDE_2)
                return -1;
            break;
        } else if (hunk == RR_SIDE_1) {
            strbuf_addbuf(buf_A, &buf);
            string_list_append(list_A,buf.buf);
        }
        else if (hunk == RR_ORIGINAL)
            ; /* discard */
        else if (hunk == RR_SIDE_2) {
            strbuf_addbuf(buf_B, &buf);
            string_list_append(list_B,buf.buf);
        }
    }
    strbuf_release(&buf);
    //fprintf_ln(stderr, _("LOG_EXIT: separate_conflict_area"));
    return 1;
}


/* cur = current file
 * pre = preimage file
 * <<<<<<<
 *    A
 * =======
 *    B
 * >>>>>>>
 */

static int control_conflict_area(struct rerere_io *cur,struct rerere_io *pre, struct rerere_io *post,
                                 struct strbuf *pre_out_buf,struct strbuf *post_out_buf,int marker_size)
{
    //fprintf_ln(stderr, _("LOG_ENTER: control_conflict_area"));

    struct rerere_io    *cur_io = cur,
            *pre_io = pre;

    struct strbuf   cur_buf_A = STRBUF_INIT, cur_buf_B = STRBUF_INIT,
            pre_buf_A = STRBUF_INIT, pre_buf_B = STRBUF_INIT,
            end_pre_buf = STRBUF_INIT,
            post_buf = STRBUF_INIT;

    struct string_list  cur_list_A = STRING_LIST_INIT_DUP, cur_list_B = STRING_LIST_INIT_DUP,
            pre_list_A = STRING_LIST_INIT_DUP, pre_list_B = STRING_LIST_INIT_DUP,
            post_list = STRING_LIST_INIT_DUP;

    enum {
        RR_SIDE_1 = 0,  // = 0
        RR_SIDE_2,      // = 1
        RR_NO_SIDE,
    } conflict_area = RR_NO_SIDE;

    rerere_strbuf_putconflict(pre_out_buf, '<', marker_size);

    // current file
    if (separate_conflict_area(cur_io,&cur_buf_A,&cur_buf_B,marker_size,&cur_list_A,&cur_list_B) < 0)
        return -1;

    // preimage file
    if (separate_conflict_area(pre_io,&pre_buf_A,&pre_buf_B,marker_size,&pre_list_A,&pre_list_B) <0)
        return -1;

    //get preimage line after '>' marker... this is necessary to understand where the conflict area ends in postimage
    pre->getline(&end_pre_buf,pre);
    //fprintf_ln(stderr, _("control_conflict_area: end_pre_buf: %s"),end_pre_buf.buf);
    // < 0 : one < two
    // = 0 : one = two
    // > 0 : one > two
    if (strbuf_cmp(&cur_buf_A, &cur_buf_B) > 0) {
        strbuf_swap(&cur_buf_A,&cur_buf_B);
        SWAP(cur_list_A,cur_list_B);
    }

    if (strbuf_cmp(&cur_buf_A, &pre_buf_B) == 0 && strbuf_cmp(&cur_buf_B, &pre_buf_A) != 0) {
        strbuf_swap(&pre_buf_A,&pre_buf_B);
        SWAP(pre_list_A,pre_list_B);
    } else if (strbuf_cmp(&cur_buf_B, &pre_buf_A) == 0 && strbuf_cmp(&cur_buf_A, &pre_buf_B) != 0) {
        strbuf_swap(&pre_buf_A,&pre_buf_B);
        SWAP(pre_list_A,pre_list_B);
    }

    //get postimage line and compare it to preimage areas to determine the area to which it belongs
    while(!post->getline(&post_buf,post)) {
        //fprintf_ln(stderr, _("control_conflict_area: post_buf: %s"),post_buf.buf);
        if (strbuf_cmp(&post_buf,&end_pre_buf) == 0) {
            break;
        }
        string_list_append(&post_list,post_buf.buf);

        if (unsorted_string_list_has_string(&pre_list_A,post_buf.buf)) { // present in part A
            if (!unsorted_string_list_has_string(&pre_list_B,post_buf.buf)) { // non present in part B
                conflict_area = RR_SIDE_1; // set side A
            }
        }else if (unsorted_string_list_has_string(&pre_list_B,post_buf.buf)) { // present in part B
            if (unsorted_string_list_has_string(&pre_list_B,post_buf.buf)) { // not present in part A
                conflict_area = RR_SIDE_2; // set side B
            }
        }
    }

    if (conflict_area == RR_NO_SIDE)
        return -1;

    // compare part A of current and preimage file
    if (control_line_character(&cur_list_A,&pre_list_A,pre_out_buf) < 0){
        return -1;
    }

    rerere_strbuf_putconflict(pre_out_buf, '=', marker_size);

    // compare part B of current and preimage file
    if (control_line_character(&cur_list_B,&pre_list_B,pre_out_buf) < 0){
        return -1;
    }

    rerere_strbuf_putconflict(pre_out_buf, '>', marker_size);

    if (conflict_area == RR_SIDE_1) {
        fprintf_ln(stderr, _("control_conflict_area: SIDE 11111111111"));
        if (control_line_character(&cur_list_A,&post_list,post_out_buf) < 0){
            return -1;
        }
    }

    if (conflict_area == RR_SIDE_2) {
        fprintf_ln(stderr, _("control_conflict_area: SIDE 222222222222"));
        if (control_line_character(&cur_list_B,&post_list,post_out_buf) < 0){
            return -1;
        }
    }

    if (end_pre_buf.len >= 0){
        strbuf_addbuf(pre_out_buf,&end_pre_buf);
        strbuf_addbuf(post_out_buf,&end_pre_buf);
    }

    strbuf_release(&cur_buf_A);
    strbuf_release(&cur_buf_B);
    strbuf_release(&pre_buf_A);
    strbuf_release(&pre_buf_B);
    strbuf_release(&end_pre_buf);
    strbuf_release(&post_buf);

    string_list_clear(&cur_list_A,1);
    string_list_clear(&cur_list_B,1);
    string_list_clear(&pre_list_A,1);
    string_list_clear(&pre_list_B,1);
    string_list_clear(&post_list,1);

    //fprintf_ln(stderr, _("LOG_EXIT: control_conflict_area"));
    return 1;
}


static int compare_n_update(struct rerere_io *cur,struct rerere_io *pre,struct rerere_io *post,
                            struct strbuf *pre_out_buf,struct strbuf *post_out_buf,int marker_size)
{
    //fprintf_ln(stderr, _("LOG_ENTER: compare_n_update"));

    struct strbuf cur_buf = STRBUF_INIT, pre_buf = STRBUF_INIT, post_buf = STRBUF_INIT;
    int pre_marker_found = 0;
    int cur_marker_found = 0;

    while (!pre->getline(&pre_buf, pre)) {
        if (my_cmarker(pre_buf.buf, '<', marker_size)) {
            pre_marker_found = 1;
            break;
        }
        //increment postimage pointer
        post->getline(&post_buf,post);

        strbuf_addbuf(pre_out_buf, &pre_buf);
        strbuf_addbuf(post_out_buf, &post_buf);
    }
    while (!cur->getline(&cur_buf, cur)) {
        if (my_cmarker(cur_buf.buf, '<', marker_size)) {
            cur_marker_found = 1;
            break;
        }
    }

    do {
        if (!pre_marker_found && !cur_marker_found) //in preimage or curimage marker not found
            return 0;

        if (control_conflict_area(cur, pre, post, pre_out_buf,post_out_buf, marker_size) < 0) {
            return -1; //TWO DIFFERENT LINE FOUND, NO FURTHER ACTION
        }

        cur_marker_found = 0;
        pre_marker_found = 0;

        while (!pre->getline(&pre_buf, pre)) {
            if (my_cmarker(pre_buf.buf, '<', marker_size)) {
                pre_marker_found = 1;
                break;
            }
            // increment postimage pointer
            post->getline(&post_buf,post);
            strbuf_addbuf(pre_out_buf, &pre_buf);
            strbuf_addbuf(post_out_buf, &post_buf);
        }
        while (!cur->getline(&cur_buf, cur)) {
            if (my_cmarker(cur_buf.buf, '<', marker_size)) {
                cur_marker_found = 1;
                break;
            }
        }
    } while (my_cmarker(cur_buf.buf, '<', marker_size) && my_cmarker(pre_buf.buf, '<', marker_size));

    strbuf_release(&cur_buf);
    strbuf_release(&pre_buf);
    strbuf_release(&post_buf);
    //fprintf_ln(stderr, _("LOG_EXIT: compare_n_update"));
    return 1;
}

/*
 * Scan the path for conflicts, do the "handle_path()" thing above, and
 * return the number of conflict hunks found.
 */
static int handle_file(struct index_state *istate,
                       const char *path, unsigned char *hash, const char *output)
{
    //fprintf_ln(stderr, _("LOG_ENTER: handle_file function"));
    int has_conflicts = 0;
    struct rerere_io_file io;
    int marker_size = ll_merge_marker_size(istate, path);

    memset(&io, 0, sizeof(io));
    io.io.getline = rerere_file_getline;
    io.input = fopen(path, "r");
    io.io.wrerror = 0;
    if (!io.input)
        return error_errno(_("could not open '%s'"), path);

    if (output) {
        io.io.output = fopen(output, "w");
        if (!io.io.output) {
            error_errno(_("could not write '%s'"), output);
            fclose(io.input);
            return -1;
        }
    }

    has_conflicts = handle_path(hash, (struct rerere_io *)&io, marker_size);

    fclose(io.input);
    if (io.io.wrerror)
        error(_("there were errors while writing '%s' (%s)"),
              path, strerror(io.io.wrerror));
    if (io.io.output && fclose(io.io.output))
        io.io.wrerror = error_errno(_("failed to flush '%s'"), path);

    if (has_conflicts < 0) {
        if (output)
            unlink_or_warn(output);
        return error(_("could not parse conflict hunks in '%s'"), path);
    }
    if (io.io.wrerror)
        return -1;
    //fprintf_ln(stderr, _("LOG_EXIT: handle_file function"));
    return has_conflicts;
}

/*
 * Look at a cache entry at "i" and see if it is not conflicting,
 * conflicting and we are willing to handle, or conflicting and
 * we are unable to handle, and return the determination in *type.
 * Return the cache index to be looked at next, by skipping the
 * stages we have already looked at in this invocation of this
 * function.
 */
static int check_one_conflict(struct index_state *istate, int i, int *type)
{
    const struct cache_entry *e = istate->cache[i];

    if (!ce_stage(e)) {
        *type = RESOLVED;
        return i + 1;
    }

    *type = PUNTED;
    while (i < istate->cache_nr && ce_stage(istate->cache[i]) == 1)
        i++;

    /* Only handle regular files with both stages #2 and #3 */
    if (i + 1 < istate->cache_nr) {
        const struct cache_entry *e2 = istate->cache[i];
        const struct cache_entry *e3 = istate->cache[i + 1];
        if (ce_stage(e2) == 2 &&
            ce_stage(e3) == 3 &&
            ce_same_name(e, e3) &&
            S_ISREG(e2->ce_mode) &&
            S_ISREG(e3->ce_mode))
            *type = THREE_STAGED;
    }

    /* Skip the entries with the same name */
    while (i < istate->cache_nr && ce_same_name(e, istate->cache[i]))
        i++;
    return i;
}

/*
 * Scan the index and find paths that have conflicts that rerere can
 * handle, i.e. the ones that has both stages #2 and #3.
 *
 * NEEDSWORK: we do not record or replay a previous "resolve by
 * deletion" for a delete-modify conflict, as that is inherently risky
 * without knowing what modification is being discarded.  The only
 * safe case, i.e. both side doing the deletion and modification that
 * are identical to the previous round, might want to be handled,
 * though.
 */
static int find_conflict(struct repository *r, struct string_list *conflict)
{
    int i;

    if (repo_read_index(r) < 0)
        return error(_("index file corrupt"));

    for (i = 0; i < r->index->cache_nr;) {
        int conflict_type;
        const struct cache_entry *e = r->index->cache[i];
        i = check_one_conflict(r->index, i, &conflict_type);
        if (conflict_type == THREE_STAGED)
            string_list_insert(conflict, (const char *)e->name);
    }
    return 0;
}

/*
 * The merge_rr list is meant to hold outstanding conflicted paths
 * that rerere could handle.  Abuse the list by adding other types of
 * entries to allow the caller to show "rerere remaining".
 *
 * - Conflicted paths that rerere does not handle are added
 * - Conflicted paths that have been resolved are marked as such
 *   by storing RERERE_RESOLVED to .util field (where conflict ID
 *   is expected to be stored).
 *
 * Do *not* write MERGE_RR file out after calling this function.
 *
 * NEEDSWORK: we may want to fix the caller that implements "rerere
 * remaining" to do this without abusing merge_rr.
 */
int rerere_remaining(struct repository *r, struct string_list *merge_rr)
{
    int i;

    if (setup_rerere(r, merge_rr, RERERE_READONLY))
        return 0;
    if (repo_read_index(r) < 0)
        return error(_("index file corrupt"));

    for (i = 0; i < r->index->cache_nr;) {
        int conflict_type;
        const struct cache_entry *e = r->index->cache[i];
        i = check_one_conflict(r->index, i, &conflict_type);
        if (conflict_type == PUNTED)
            string_list_insert(merge_rr, (const char *)e->name);
        else if (conflict_type == RESOLVED) {
            struct string_list_item *it;
            it = string_list_lookup(merge_rr, (const char *)e->name);
            if (it != NULL) {
                free_rerere_id(it);
                it->util = RERERE_RESOLVED;
            }
        }
    }
    return 0;
}

/*
 * Try using the given conflict resolution "ID" to see
 * if that recorded conflict resolves cleanly what we
 * got in the "cur".
 */
static int try_merge(struct index_state *istate,
                     const struct rerere_id *id, const char *path,
                     mmfile_t *cur, mmbuffer_t *result)
{
    //fprintf_ln(stderr, _("LOG_ENTER: try_merge function"));
    int ret;
    mmfile_t base = {NULL, 0}, other = {NULL, 0};

    if (read_mmfile(&base, rerere_path(id, "preimage")) ||
        read_mmfile(&other, rerere_path(id, "postimage")))
        ret = 1;
    else
        /*
         * A three-way merge. Note that this honors user-customizable
         * low-level merge driver settings.
         */
        ret = ll_merge(result, path, &base, NULL, cur, "", &other, "",
                       istate, NULL);

    free(base.ptr);
    free(other.ptr);
    //fprintf_ln(stderr, _("LOG_EXIT: try_merge function"));
    return ret;
}

/*
 * Find the conflict identified by "id"; the change between its
 * "preimage" (i.e. a previous contents with conflict markers) and its
 * "postimage" (i.e. the corresponding contents with conflicts
 * resolved) may apply cleanly to the contents stored in "path", i.e.
 * the conflict this time around.
 *
 * Returns 0 for successful replay of recorded resolution, or non-zero
 * for failure.
 */
static int merge(struct index_state *istate, const struct rerere_id *id, const char *path)
{
    //fprintf_ln(stderr, _("LOG_ENTER: merge function"));
    FILE *f;
    int ret;
    mmfile_t cur = {NULL, 0};
    mmbuffer_t result = {NULL, 0};

    /*
     * Normalize the conflicts in path and write it out to
     * "thisimage" temporary file.
     */
    if ((handle_file(istate, path, NULL, rerere_path(id, "thisimage")) < 0) ||
        read_mmfile(&cur, rerere_path(id, "thisimage"))) {
        ret = 1;
        goto out;
    }

    ret = try_merge(istate, id, path, &cur, &result);
    if (ret)
        goto out;

    /*
     * A successful replay of recorded resolution.
     * Mark that "postimage" was used to help gc.
     */
    if (utime(rerere_path(id, "postimage"), NULL) < 0)
        warning_errno(_("failed utime() on '%s'"),
                      rerere_path(id, "postimage"));

    /* Update "path" with the resolution */
    f = fopen(path, "w");
    if (!f)
        return error_errno(_("could not open '%s'"), path);
    if (fwrite(result.ptr, result.size, 1, f) != 1)
        error_errno(_("could not write '%s'"), path);
    if (fclose(f))
        return error_errno(_("writing '%s' failed"), path);

    out:
    free(cur.ptr);
    free(result.ptr);
    //fprintf_ln(stderr, _("LOG_EXIT: merge function"));
    return ret;
}

static void update_paths(struct repository *r, struct string_list *update)
{
    struct lock_file index_lock = LOCK_INIT;
    int i;

    repo_hold_locked_index(r, &index_lock, LOCK_DIE_ON_ERROR);

    for (i = 0; i < update->nr; i++) {
        struct string_list_item *item = &update->items[i];
        if (add_file_to_index(r->index, item->string, 0))
            exit(128);
        fprintf_ln(stderr, _("Staged '%s' using previous resolution."),
                   item->string);
    }

    if (write_locked_index(r->index, &index_lock,
                           COMMIT_LOCK | SKIP_IF_UNCHANGED))
        die(_("unable to write new index file"));
}

static void remove_variant(struct rerere_id *id)
{
    unlink_or_warn(rerere_path(id, "postimage"));
    unlink_or_warn(rerere_path(id, "preimage"));
    id->collection->status[id->variant] = 0;
}

/*
 * control if the hash of the conflict area is changed or not,
 * if yes then control if what is the difference between preimage and current file
 */
static int check_hash_change(struct index_state *istate,
                             const char *path,char* old_hash,char * new_hash)
{
    //fprintf_ln(stderr, _("LOG_ENTER: check_hash_change"));

    struct rerere_id *old_id = new_rerere_id_hex(old_hash);
    struct rerere_id *new_id = new_rerere_id_hex(new_hash);
    int marker_size = ll_merge_marker_size(istate, path);

    for (int variant = 0; variant < old_id->collection->status_nr; variant++) {

        const int both = RR_HAS_PREIMAGE | RR_HAS_POSTIMAGE;
        struct rerere_id vold_id = *old_id;

        if ((old_id->collection->status[variant] & both) != both)
            continue;
        vold_id.variant = variant;

        if (handle_file(istate, path, NULL, rerere_path(&vold_id, "curimage")) < 0) {
            return  0;
        }

        struct rerere_io_file cur;
        const char *cur_path = rerere_path(&vold_id, "curimage");
        memset(&cur, 0, sizeof(cur));
        cur.io.getline = rerere_file_getline;
        cur.input = fopen(cur_path, "r");
        cur.io.wrerror = 0;
        if (!cur.input)
            return error_errno(_("could not open '%s'"), cur_path);

        struct rerere_io_file pre;
        const char *pre_path = rerere_path(&vold_id, "preimage");
        memset(&pre, 0, sizeof(pre));
        pre.io.getline = rerere_file_getline;
        pre.input = fopen(pre_path, "r");
        pre.io.wrerror = 0;
        if (!pre.input)
            return error_errno(_("could not open '%s'"), pre_path);

        struct rerere_io_file post;
        const char *post_path = rerere_path(&vold_id, "postimage");
        memset(&post, 0, sizeof(post));
        post.io.getline = rerere_file_getline;
        post.input = fopen(post_path, "r");
        post.io.wrerror = 0;
        if (!post.input)
            return error_errno(_("could not open '%s'"), post_path);

        struct strbuf pre_out_buf = STRBUF_INIT;
        struct strbuf post_out_buf = STRBUF_INIT;

        int res = compare_n_update((struct rerere_io *)&cur,(struct rerere_io *)&pre,(struct rerere_io *)&post,
                                   &pre_out_buf,&post_out_buf,marker_size);

        if (res > 0){
            struct rerere_io_file out;
            const char *out_path = rerere_path(new_id, "preimage");
            mkdir_in_gitdir(rerere_path(new_id, NULL));
            memset(&out, 0, sizeof(out));
            out.io.output = fopen(out_path, "w");
            if (!out.io.output) {
                error_errno(_("could not write '%s'"), out_path);
            }

            strbuf_write(&pre_out_buf,out.io.output);

            if (out.io.wrerror)
                error(_("there were errors while writing '%s' (%s)"),
                      out_path, strerror(out.io.wrerror));
            if (out.io.output && fclose(out.io.output))
                out.io.wrerror = error_errno(_("failed to flush '%s'"), out_path);

            //copy_file(rerere_path(new_id, "postimage"), post_path, 0666);
            //.......................POSTIMAGE .................................

            out_path = rerere_path(new_id, "postimage");
            mkdir_in_gitdir(rerere_path(new_id, NULL));
            memset(&out, 0, sizeof(out));
            out.io.output = fopen(out_path, "w");
            if (!out.io.output) {
                error_errno(_("could not write '%s'"), out_path);
            }

            strbuf_write(&post_out_buf,out.io.output);

            if (out.io.wrerror)
                error(_("there were errors while writing '%s' (%s)"),
                      out_path, strerror(out.io.wrerror));
            if (out.io.output && fclose(out.io.output))
                out.io.wrerror = error_errno(_("failed to flush '%s'"), out_path);
            break;
        }
        unlink_or_warn(rerere_path(&vold_id, "curimage"));
        free_rerere_dirs();
        fclose(cur.input);
        fclose(pre.input);
        fclose(post.input);
        strbuf_release(&pre_out_buf);
        strbuf_release(&post_out_buf);
        break;
    }
    //fprintf_ln(stderr, _("LOG_EXIT: check_hash_change"));
    return  1;
}

/*
 * write or update path and hash index file
 */
static int hash_index_file(const char *fname,const char *path,const char *hash) {

    //fprintf_ln(stderr, _("LOG ENTER: hash_index_file "));

    int found = 0;
    char buffer[256];

    //fprintf_ln(stderr, _("hash_index_file: PATH: %s "),path);
    //fprintf_ln(stderr, _("hash_index_file: HASH: %s "),hash);
    struct strbuf buf = STRBUF_INIT;
    struct rerere_io_file io;
    memset(&io, 0, sizeof(io));
    io.io.getline = rerere_file_getline;
    io.input = fopen(fname, "r");

    FILE *temp;
    temp = fopen(".git/rr-cache/temp", "w");

    io.io.wrerror = 0;
    if (io.input && temp) {
        while (!io.io.getline(&buf, &io.io)) {

            if (strstr(buf.buf, path) != NULL) { // if path is already present in file then update it
                found = 1;
                fprintf(temp, "%s;%s\n", path, hash); // write in temp file
            } else {
                fprintf(temp, "%s", buf.buf); // write in temp file
            }
        }
        fclose(io.input);
        fclose(temp);
    }

    if (!found) { //not found, add new line

        //fprintf_ln(stderr, _("hash_index_file: NOT FOUND !!! "));
        FILE *input  = fopen(fname, "a+");

        if(input) {
            fprintf(input, "%s;%s\n", path, hash);
            fclose(input);
        }

    } else { // found, copy temp

        //fprintf_ln(stderr, _("hash_index_file: FOUND !!! "));
        FILE *input  = fopen(fname, "w");
        temp = fopen(".git/rr-cache/temp", "r");

        if (input && temp) {

            while (fgets(buffer, sizeof(buffer), temp))
                fputs(buffer, input);

            fclose(temp);
            fclose(input);
        }
    }
    unlink_or_warn(".git/rr-cache/temp");

    //fprintf_ln(stderr, _("LOG EXIT: hash_index_file "));
    return found;
}

/*
 * write or update conflict index file
 */
static int conflict_index_file(struct rerere_id *id, int marker_size)
{
    //fprintf_ln(stderr, _("LOG_ENTER: conflict_index_file function"));

    struct strbuf pre_buf = STRBUF_INIT;
    //int pre_marker_found = 1;
    struct strbuf pre_buf_A = STRBUF_INIT, pre_buf_B = STRBUF_INIT, post_buf = STRBUF_INIT, post_buf_out = STRBUF_INIT;
    struct string_list pre_list_A = STRING_LIST_INIT_DUP, pre_list_B = STRING_LIST_INIT_DUP, post_list = STRING_LIST_INIT_DUP;
    enum {
        RR_SIDE_1 = 0,  // = 0
        RR_SIDE_2,      // = 1
        RR_NO_SIDE,
    } conflict_area = RR_NO_SIDE;

    struct rerere_io_file pre;
    const char *pre_path = rerere_path(id, "preimage");
    memset(&pre, 0, sizeof(pre));
    pre.io.getline = rerere_file_getline;
    pre.input = fopen(pre_path, "r");
    pre.io.wrerror = 0;
    if (!pre.input)
        return error_errno(_("could not open '%s'"), pre_path);

    struct rerere_io_file post;
    const char *post_path = rerere_path(id, "postimage");
    memset(&post, 0, sizeof(post));
    post.io.getline = rerere_file_getline;
    post.input = fopen(post_path, "r");
    post.io.wrerror = 0;
    if (!post.input)
        return error_errno(_("could not open '%s'"), post_path);

    while (!pre.io.getline(&pre_buf, &pre.io)) {

        if (my_cmarker(pre_buf.buf, '<', marker_size)) {
            separate_conflict_area(&pre.io,&pre_buf_A,&pre_buf_B,marker_size,&pre_list_A,&pre_list_B);

            // get preimage line next to end marker >>>>>>, we need it to determine where the conflict area finish in postimage
            pre.io.getline(&pre_buf, &pre.io);

            //get postimage line and compare it to preimage areas to determine the area to which it belongs
            while(!post.io.getline(&post_buf,&post.io)) {
                //fprintf_ln(stderr, _("conflict_index_file: post_buf: %s"),post_buf.buf);
                if (strbuf_cmp(&post_buf,&pre_buf) == 0) {
                    break;
                }
                string_list_append(&post_list,post_buf.buf);
                strbuf_addbuf(&post_buf_out,&post_buf);

                if (unsorted_string_list_has_string(&pre_list_A,post_buf.buf)) { // present in part A
                    if (!unsorted_string_list_has_string(&pre_list_B,post_buf.buf)) { // non present in part B
                        conflict_area = RR_SIDE_1; // set side A
                    }
                }else if (unsorted_string_list_has_string(&pre_list_B,post_buf.buf)) { // present in part B
                    if (unsorted_string_list_has_string(&pre_list_B,post_buf.buf)) { // not present in part A
                        conflict_area = RR_SIDE_2; // set side B
                    }
                }
            }

            if (conflict_area == RR_SIDE_1) {
                //write_conflict_index(pre_buf_B.buf,post_buf_out.buf);
                if(!is_multiline_string(pre_buf_B.buf) && !is_multiline_string(post_buf_out.buf)) {
                    strbuf_trim(&pre_buf_B);
                    strbuf_trim(&post_buf_out);
                    strbuf_trim_trailing_newline(&pre_buf_B);
                    strbuf_trim_trailing_newline(&post_buf_out);
                    write_json_conflict_index(pre_buf_B.buf, post_buf_out.buf);
                }
            }

            if (conflict_area == RR_SIDE_2) {
                //write_conflict_index(pre_buf_A.buf,post_buf_out.buf);
                if(!is_multiline_string(pre_buf_A.buf) && !is_multiline_string(post_buf_out.buf)) {
                    strbuf_trim(&pre_buf_A);
                    strbuf_trim(&post_buf_out);
                    strbuf_trim_trailing_newline(&pre_buf_A);
                    strbuf_trim_trailing_newline(&post_buf_out);
                    write_json_conflict_index(pre_buf_A.buf, post_buf_out.buf);
                }
            }
        } else {
            //increment postimage pointer
            post.io.getline(&post_buf, &post.io);
        }
        strbuf_reset(&pre_buf);
        strbuf_reset(&pre_buf_A);
        strbuf_reset(&pre_buf_B);
        strbuf_reset(&post_buf);
        strbuf_reset(&post_buf_out);

        string_list_init(&pre_list_A,1);
        string_list_init(&pre_list_B,1);
        string_list_init(&post_list,1);
    }

    strbuf_release(&pre_buf);
    strbuf_release(&pre_buf_A);
    strbuf_release(&pre_buf_B);
    strbuf_release(&post_buf);
    strbuf_release(&post_buf_out);

    string_list_clear(&pre_list_A,1);
    string_list_clear(&pre_list_B,1);
    string_list_clear(&post_list,1);

    fclose(pre.input);
    fclose(post.input);

    //fprintf_ln(stderr, _("LOG_EXIT: conflict_index_file function"));
    return 1;
}

static int check_conflict_suggestion(struct index_state *istate, struct rerere_id *id,const char* path)
{
    //fprintf_ln(stderr, _("LOG_ENTER: check_conflict_suggestion"));
    if(access(git_path("rr-cache/%s", "conflict_index.json"), F_OK ) == -1) {
        fprintf_ln(stderr, _("check_conflict_suggestion: No file Conflict_index.json"));
        return 0;
    }

    if (handle_file(istate, path, NULL, rerere_path(id, "curimage")) < 0) {
        return  0;
    }

    struct strbuf cur_buf = STRBUF_INIT;

    struct strbuf cur_buf_A = STRBUF_INIT, cur_buf_B = STRBUF_INIT;
    struct string_list cur_list_A = STRING_LIST_INIT_DUP, cur_list_B = STRING_LIST_INIT_DUP;
    int marker_size = ll_merge_marker_size(istate, path);

    struct rerere_io_file cur;
    const char *cur_path = rerere_path(id, "curimage");
    memset(&cur, 0, sizeof(cur));
    cur.io.getline = rerere_file_getline;
    cur.input = fopen(cur_path, "r");
    cur.io.wrerror = 0;
    if (!cur.input)
        return error_errno(_("could not open '%s'"), cur_path);

    while (!cur.io.getline(&cur_buf, &cur.io)) {
        if (my_cmarker(cur_buf.buf, '<', marker_size)) {
            separate_conflict_area(&cur.io, &cur_buf_A, &cur_buf_B, marker_size, &cur_list_A, &cur_list_B);
            //conflict_suggestion(cur_buf_A.buf);
            //conflict_suggestion(cur_buf_B.buf);
            if(!is_multiline_string(cur_buf_A.buf) && !is_multiline_string(cur_buf_B.buf)) {
                strbuf_trim(&cur_buf_A);
                strbuf_trim_trailing_newline(&cur_buf_A);
                regex_repalce_suggestion(cur_buf_A.buf);
                strbuf_trim(&cur_buf_B);
                strbuf_trim_trailing_newline(&cur_buf_B);
                regex_repalce_suggestion(cur_buf_B.buf);
            }
        }
        strbuf_reset(&cur_buf_A);
        strbuf_reset(&cur_buf_B);
    }

    strbuf_release(&cur_buf);
    strbuf_release(&cur_buf_A);
    strbuf_release(&cur_buf_B);
    string_list_clear(&cur_list_A,1);
    string_list_clear(&cur_list_B,1);

    fclose(cur.input);
    unlink_or_warn(rerere_path(id, "curimage"));
    //fprintf_ln(stderr, _("LOG_EXIT: check_conflict_suggestion function"));
    return 1;
}

static void executeRegexJar()
{
    //fprintf_ln(stderr, _("LOG_ENTER: executeRegexJar"));
    if (groupId_list.nr) {
        int length = 5 + groupId_list.nr; //groupId_list.nr know only at runtime
        const char **id_array = malloc(sizeof(*id_array) * length);

        id_array[0] = "/usr/bin/java";
        id_array[1] = "-jar";
        id_array[2] = "/Users/manan/CLionProjects/git/search-and-replace/RandomSearchReplaceTurtle.jar";
        id_array[3] = "/Users/manan/CLionProjects/git/search-and-replace/"; //config.properties path

        fprintf_ln(stderr, _("GROUD_LIST_NR: %u"),groupId_list.nr);
        for (int j = 0; j < groupId_list.nr; j++) {
            id_array[j+4] = groupId_list.items[j].string;
        }
        id_array[length - 1] = NULL; //terminator need for execv

        //TODO adjust the path to jar file
        pid_t pid = fork();
        //fprintf_ln(stderr, _("AFTER PID FORK:"));
        if (pid == 0) { // child process
            /* open /dev/null for writing */
            int fd = open("/dev/null", O_WRONLY);
            dup2(fd, 1);    /* make stdout a copy of fd (> /dev/null) */
            //dup2(fd, 2);    /* ...and same with stderr */
            close(fd);
            fprintf_ln(stderr, _("\n\n\nJAR PROCESS IS STARTING IN BACKGROUND!!!!!\n\n\n"));
            execv("/usr/bin/java",(void*)id_array);
        } else { //parent process
            //int status;
            //(void)waitpid(pid, &status, 0);
            //fprintf_ln(stderr, _("\n\n\nGIT PROCESS GO ON !!!!!\n\n\n"));
        }
        free(id_array);
    }
    //fprintf_ln(stderr, _("LOG_EXIT: executeRegexJar"));
    return;
}

/*
 * The path indicated by rr_item may still have conflict for which we
 * have a recorded resolution, in which case replay it and optionally
 * update it.  Or it may have been resolved by the user and we may
 * only have the preimage for that conflict, in which case the result
 * needs to be recorded as a resolution in a postimage file.
 */
static void do_rerere_one_path(struct index_state *istate,
                               struct string_list_item *rr_item,
                               struct string_list *update)
{
    //fprintf_ln(stderr, _("LOG_ENTER: do_rerere_one_path function"));
    const char *path = rr_item->string;
    struct rerere_id *id = rr_item->util;
    struct rerere_dir *rr_dir = id->collection;
    int variant;

    variant = id->variant;

    const char *hash = rerere_id_hex(id);
//    fprintf_ln(stderr, _("VARIANT: %d"),variant);
//    fprintf_ln(stderr, _("PATH: %s"),path);
    /* Has the user resolved it already? */
    if (variant >= 0) {
        if (!handle_file(istate, path, NULL, NULL)) {
            copy_file(rerere_path(id, "postimage"), path, 0666);
            id->collection->status[variant] |= RR_HAS_POSTIMAGE;
            fprintf_ln(stderr, _("Recorded resolution for '%s'."), path);

            hash_index_file(".git/rr-cache/hash_index",path,hash);

            int marker_size = ll_merge_marker_size(istate, path);
            conflict_index_file(id,marker_size);

            free_rerere_id(rr_item);
            rr_item->util = NULL;
            return;
        }
        /*
         * There may be other variants that can cleanly
         * replay.  Try them and update the variant number for
         * this one.
         */
    }

    /* Does any existing resolution apply cleanly? */
    for (variant = 0; variant < rr_dir->status_nr; variant++) {
        //fprintf_ln(stderr, _("/* Does any existing resolution apply cleanly? */ variant: '%d'"), variant);
        const int both = RR_HAS_PREIMAGE | RR_HAS_POSTIMAGE;
        struct rerere_id vid = *id;

        if ((rr_dir->status[variant] & both) != both)
            continue;

        vid.variant = variant;
        //fprintf_ln(stderr, _("/* calling function merge()"));
        if (merge(istate, &vid, path))
            continue; /* failed to replay */

        hash_index_file(".git/rr-cache/hash_index",path,hash);

        /*
         * If there already is a different variant that applies
         * cleanly, there is no point maintaining our own variant.
         */
        if (0 <= id->variant && id->variant != variant)
            remove_variant(id);

        if (rerere_autoupdate)
            string_list_insert(update, path);
        else
            fprintf_ln(stderr,
                       _("Resolved '%s' using previous resolution."),
                       path);
        free_rerere_id(rr_item);
        rr_item->util = NULL;
        return;
    }

    /* None of the existing one applies; we need a new variant */
    assign_variant(id);
    //fprintf_ln(stderr, _("/* None of the existing one applies; we need a new variant */"));
    variant = id->variant;
    handle_file(istate, path, NULL, rerere_path(id, "preimage"));
    if (id->collection->status[variant] & RR_HAS_POSTIMAGE) {
        const char *path = rerere_path(id, "postimage");
        if (unlink(path))
            die_errno(_("cannot unlink stray '%s'"), path);
        id->collection->status[variant] &= ~RR_HAS_POSTIMAGE;
    }
    id->collection->status[variant] |= RR_HAS_PREIMAGE;
    fprintf_ln(stderr, _("Recorded preimage for '%s'"), path);
}

static int do_plain_rerere(struct repository *r,
                           struct string_list *rr, int fd)
{
    fprintf_ln(stderr, _("______LOG_ENTER: do_plain_rerere function"));

    struct string_list conflict = STRING_LIST_INIT_DUP;
    struct string_list update = STRING_LIST_INIT_DUP;
    int i;

    find_conflict(r, &conflict);
    string_list_init(&groupId_list,1);
    /*
     * MERGE_RR records paths with conflicts immediately after
     * merge failed.  Some of the conflicted paths might have been
     * hand resolved in the working tree since then, but the
     * initial run would catch all and register their preimages.
     */
    for (i = 0; i < conflict.nr; i++) {
        struct rerere_id *id;
        unsigned char hash[GIT_MAX_RAWSZ];
        const char *path = conflict.items[i].string;
        int ret;

        /*
         * Ask handle_file() to scan and assign a
         * conflict ID.  No need to write anything out
         * yet.
         */
        ret = handle_file(r->index, path, hash, NULL);
        if (ret != 0 && string_list_has_string(rr, path)) {
            remove_variant(string_list_lookup(rr, path)->util);
            string_list_remove(rr, path, 1);
        }
        if (ret < 1)
            continue;

        char *old_hash = get_file_hash(path);
        //fprintf_ln(stderr, _("old hash: %s"),old_hash);
        //fprintf_ln(stderr, _("new hash: %s"),sha1_to_hex(hash));

        if (old_hash && strcmp(old_hash,sha1_to_hex(hash)) != 0) {
            //check_hash_change(r->index,path, old_hash,sha1_to_hex(hash));
        }

        id = new_rerere_id(hash); //create new id here !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
        string_list_insert(rr, path)->util = id;

        /* Ensure that the directory exists. */
        mkdir_in_gitdir(rerere_path(id, NULL));

        //if (!old_hash) {
        check_conflict_suggestion(r->index,id,path);
        //}
    }

    for (i = 0; i < rr->nr; i++) {
        do_rerere_one_path(r->index, &rr->items[i], &update);
        //fprintf_ln(stderr, _("LOG_EXIT: do_rerere_one_path function"));
    }

    executeRegexJar();

    if (update.nr)
        update_paths(r, &update);

    fprintf_ln(stderr, _("______LOG_EXIT: do_plain_rerere function before write_rr(rr, fd)"));
    return write_rr(rr, fd);
}

static void git_rerere_config(void)
{
    git_config_get_bool("rerere.enabled", &rerere_enabled);
    git_config_get_bool("rerere.autoupdate", &rerere_autoupdate);
    git_config(git_default_config, NULL);
}

static GIT_PATH_FUNC(git_path_rr_cache, "rr-cache")

static int is_rerere_enabled(void)
{
    int rr_cache_exists;

    if (!rerere_enabled)
        return 0;

    rr_cache_exists = is_directory(git_path_rr_cache());
    if (rerere_enabled < 0)
        return rr_cache_exists;

    if (!rr_cache_exists && mkdir_in_gitdir(git_path_rr_cache()))
        die(_("could not create directory '%s'"), git_path_rr_cache());
    return 1;
}

int setup_rerere(struct repository *r, struct string_list *merge_rr, int flags)
{
    int fd;

    git_rerere_config();
    if (!is_rerere_enabled())
        return -1;

    if (flags & (RERERE_AUTOUPDATE|RERERE_NOAUTOUPDATE))
        rerere_autoupdate = !!(flags & RERERE_AUTOUPDATE);
    if (flags & RERERE_READONLY)
        fd = 0;
    else
        fd = hold_lock_file_for_update(&write_lock,
                                       git_path_merge_rr(r),
                                       LOCK_DIE_ON_ERROR);
    read_rr(r, merge_rr);
    return fd;
}

/*
 * The main entry point that is called internally from codepaths that
 * perform mergy operations, possibly leaving conflicted index entries
 * and working tree files.
 */
int repo_rerere(struct repository *r, int flags)
{
    fprintf_ln(stderr, _("LOG_ENTER: repo_rerere function"));

    struct string_list merge_rr = STRING_LIST_INIT_DUP;
    int fd, status;

    fd = setup_rerere(r, &merge_rr, flags);
    if (fd < 0)
        return 0;
    status = do_plain_rerere(r, &merge_rr, fd);
    free_rerere_dirs();
    fprintf_ln(stderr, _("LOG_EXIT: repo_rerere function"));
    return status;
}

/*
 * Subclass of rerere_io that reads from an in-core buffer that is a
 * strbuf
 */
struct rerere_io_mem {
    struct rerere_io io;
    struct strbuf input;
};

/*
 * ... and its getline() method implementation
 */
static int rerere_mem_getline(struct strbuf *sb, struct rerere_io *io_)
{
    struct rerere_io_mem *io = (struct rerere_io_mem *)io_;
    char *ep;
    size_t len;

    strbuf_release(sb);
    if (!io->input.len)
        return -1;
    ep = memchr(io->input.buf, '\n', io->input.len);
    if (!ep)
        ep = io->input.buf + io->input.len;
    else if (*ep == '\n')
        ep++;
    len = ep - io->input.buf;
    strbuf_add(sb, io->input.buf, len);
    strbuf_remove(&io->input, 0, len);
    return 0;
}

static int handle_cache(struct index_state *istate,
                        const char *path, unsigned char *hash, const char *output)
{
    mmfile_t mmfile[3] = {{NULL}};
    mmbuffer_t result = {NULL, 0};
    const struct cache_entry *ce;
    int pos, len, i, has_conflicts;
    struct rerere_io_mem io;
    int marker_size = ll_merge_marker_size(istate, path);

    /*
     * Reproduce the conflicted merge in-core
     */
    len = strlen(path);
    pos = index_name_pos(istate, path, len);
    if (0 <= pos)
        return -1;
    pos = -pos - 1;

    while (pos < istate->cache_nr) {
        enum object_type type;
        unsigned long size;

        ce = istate->cache[pos++];
        if (ce_namelen(ce) != len || memcmp(ce->name, path, len))
            break;
        i = ce_stage(ce) - 1;
        if (!mmfile[i].ptr) {
            mmfile[i].ptr = read_object_file(&ce->oid, &type,
                                             &size);
            mmfile[i].size = size;
        }
    }
    for (i = 0; i < 3; i++)
        if (!mmfile[i].ptr && !mmfile[i].size)
            mmfile[i].ptr = xstrdup("");

    /*
     * NEEDSWORK: handle conflicts from merges with
     * merge.renormalize set, too?
     */
    ll_merge(&result, path, &mmfile[0], NULL,
             &mmfile[1], "ours",
             &mmfile[2], "theirs",
             istate, NULL);
    for (i = 0; i < 3; i++)
        free(mmfile[i].ptr);

    memset(&io, 0, sizeof(io));
    io.io.getline = rerere_mem_getline;
    if (output)
        io.io.output = fopen(output, "w");
    else
        io.io.output = NULL;
    strbuf_init(&io.input, 0);
    strbuf_attach(&io.input, result.ptr, result.size, result.size);

    /*
     * Grab the conflict ID and optionally write the original
     * contents with conflict markers out.
     */
    has_conflicts = handle_path(hash, (struct rerere_io *)&io, marker_size);
    strbuf_release(&io.input);
    if (io.io.output)
        fclose(io.io.output);
    return has_conflicts;
}

static int rerere_forget_one_path(struct index_state *istate,
                                  const char *path,
                                  struct string_list *rr)
{
    const char *filename;
    struct rerere_id *id;
    unsigned char hash[GIT_MAX_RAWSZ];
    int ret;
    struct string_list_item *item;

    /*
     * Recreate the original conflict from the stages in the
     * index and compute the conflict ID
     */
    ret = handle_cache(istate, path, hash, NULL);
    if (ret < 1)
        return error(_("could not parse conflict hunks in '%s'"), path);

    /* Nuke the recorded resolution for the conflict */
    id = new_rerere_id(hash);

    for (id->variant = 0;
         id->variant < id->collection->status_nr;
         id->variant++) {
        mmfile_t cur = { NULL, 0 };
        mmbuffer_t result = {NULL, 0};
        int cleanly_resolved;

        if (!has_rerere_resolution(id))
            continue;

        handle_cache(istate, path, hash, rerere_path(id, "thisimage"));
        if (read_mmfile(&cur, rerere_path(id, "thisimage"))) {
            free(cur.ptr);
            error(_("failed to update conflicted state in '%s'"), path);
            goto fail_exit;
        }
        cleanly_resolved = !try_merge(istate, id, path, &cur, &result);
        free(result.ptr);
        free(cur.ptr);
        if (cleanly_resolved)
            break;
    }

    if (id->collection->status_nr <= id->variant) {
        error(_("no remembered resolution for '%s'"), path);
        goto fail_exit;
    }

    filename = rerere_path(id, "postimage");
    if (unlink(filename)) {
        if (errno == ENOENT)
            error(_("no remembered resolution for '%s'"), path);
        else
            error_errno(_("cannot unlink '%s'"), filename);
        goto fail_exit;
    }

    /*
     * Update the preimage so that the user can resolve the
     * conflict in the working tree, run us again to record
     * the postimage.
     */
    handle_cache(istate, path, hash, rerere_path(id, "preimage"));
    fprintf_ln(stderr, _("Updated preimage for '%s'"), path);

    /*
     * And remember that we can record resolution for this
     * conflict when the user is done.
     */
    item = string_list_insert(rr, path);
    free_rerere_id(item);
    item->util = id;
    fprintf(stderr, _("Forgot resolution for '%s'\n"), path);
    return 0;

    fail_exit:
    free(id);
    return -1;
}

int rerere_forget(struct repository *r, struct pathspec *pathspec)
{
    int i, fd;
    struct string_list conflict = STRING_LIST_INIT_DUP;
    struct string_list merge_rr = STRING_LIST_INIT_DUP;

    if (repo_read_index(r) < 0)
        return error(_("index file corrupt"));

    fd = setup_rerere(r, &merge_rr, RERERE_NOAUTOUPDATE);
    if (fd < 0)
        return 0;

    /*
     * The paths may have been resolved (incorrectly);
     * recover the original conflicted state and then
     * find the conflicted paths.
     */
    unmerge_index(r->index, pathspec);
    find_conflict(r, &conflict);
    for (i = 0; i < conflict.nr; i++) {
        struct string_list_item *it = &conflict.items[i];
        if (!match_pathspec(r->index, pathspec, it->string,
                            strlen(it->string), 0, NULL, 0))
            continue;
        rerere_forget_one_path(r->index, it->string, &merge_rr);
    }
    return write_rr(&merge_rr, fd);
}

/*
 * Garbage collection support
 */

static timestamp_t rerere_created_at(struct rerere_id *id)
{
    struct stat st;

    return stat(rerere_path(id, "preimage"), &st) ? (time_t) 0 : st.st_mtime;
}

static timestamp_t rerere_last_used_at(struct rerere_id *id)
{
    struct stat st;

    return stat(rerere_path(id, "postimage"), &st) ? (time_t) 0 : st.st_mtime;
}

/*
 * Remove the recorded resolution for a given conflict ID
 */
static void unlink_rr_item(struct rerere_id *id)
{
    unlink_or_warn(rerere_path(id, "thisimage"));
    remove_variant(id);
    id->collection->status[id->variant] = 0;
}

static void prune_one(struct rerere_id *id,
                      timestamp_t cutoff_resolve, timestamp_t cutoff_noresolve)
{
    timestamp_t then;
    timestamp_t cutoff;

    then = rerere_last_used_at(id);
    if (then)
        cutoff = cutoff_resolve;
    else {
        then = rerere_created_at(id);
        if (!then)
            return;
        cutoff = cutoff_noresolve;
    }
    if (then < cutoff)
        unlink_rr_item(id);
}

void rerere_gc(struct repository *r, struct string_list *rr)
{
    struct string_list to_remove = STRING_LIST_INIT_DUP;
    DIR *dir;
    struct dirent *e;
    int i;
    timestamp_t now = time(NULL);
    timestamp_t cutoff_noresolve = now - 15 * 86400;
    timestamp_t cutoff_resolve = now - 60 * 86400;

    if (setup_rerere(r, rr, 0) < 0)
        return;

    git_config_get_expiry_in_days("gc.rerereresolved", &cutoff_resolve, now);
    git_config_get_expiry_in_days("gc.rerereunresolved", &cutoff_noresolve, now);
    git_config(git_default_config, NULL);
    dir = opendir(git_path("rr-cache"));
    if (!dir)
        die_errno(_("unable to open rr-cache directory"));
    /* Collect stale conflict IDs ... */
    while ((e = readdir(dir))) {
        struct rerere_dir *rr_dir;
        struct rerere_id id;
        int now_empty;

        if (is_dot_or_dotdot(e->d_name))
            continue;
        rr_dir = find_rerere_dir(e->d_name);
        if (!rr_dir)
            continue; /* or should we remove e->d_name? */

        now_empty = 1;
        for (id.variant = 0, id.collection = rr_dir;
             id.variant < id.collection->status_nr;
             id.variant++) {
            prune_one(&id, cutoff_resolve, cutoff_noresolve);
            if (id.collection->status[id.variant])
                now_empty = 0;
        }
        if (now_empty)
            string_list_append(&to_remove, e->d_name);
    }
    closedir(dir);

    /* ... and then remove the empty directories */
    for (i = 0; i < to_remove.nr; i++)
        rmdir(git_path("rr-cache/%s", to_remove.items[i].string));
    string_list_clear(&to_remove, 0);
    rollback_lock_file(&write_lock);
}

/*
 * During a conflict resolution, after "rerere" recorded the
 * preimages, abandon them if the user did not resolve them or
 * record their resolutions.  And drop $GIT_DIR/MERGE_RR.
 *
 * NEEDSWORK: shouldn't we be calling this from "reset --hard"?
 */
void rerere_clear(struct repository *r, struct string_list *merge_rr)
{
    int i;

    if (setup_rerere(r, merge_rr, 0) < 0)
        return;

    for (i = 0; i < merge_rr->nr; i++) {
        struct rerere_id *id = merge_rr->items[i].util;
        if (!has_rerere_resolution(id)) {
            unlink_rr_item(id);
            rmdir(rerere_path(id, NULL));
        }
    }
    unlink_or_warn(git_path_merge_rr(r));
    rollback_lock_file(&write_lock);
}
