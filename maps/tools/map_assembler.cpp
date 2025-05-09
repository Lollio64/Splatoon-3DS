
//
// Created by ash on 10/11/22.
//

#define _POSIX_C_SOURCE 200809L
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <list>
#include <string>
#include <memory>
#include <vector>

static inline int
alloc_line(char**  pline, size_t*  plsize, size_t newsize) {
  void* n = realloc(*pline, newsize);
  if (n == NULL) return -1;
  *pline = (char*)n;
  *plsize = newsize;
  return 0;
}

// implement POSIX getline() with fgets()
static ssize_t
getline1(char** pline, size_t*  plsize, FILE* fp) {
  //if (*plsize < 128) if (alloc_line(pline, plsize, 128)) return -1;
  if (*plsize < 2) if (alloc_line(pline, plsize, 2)) return -1;
  if (*pline == NULL) return -1;
  if (feof(fp)) return -1;
  char* cur = *pline;
  size_t cursize = *plsize;
  for (;;) {
    char* ret = fgets(cur, cursize, fp);
    if (ret == NULL && !feof(fp)) return -1; //=> read error
    if (ret == NULL && cur == *pline) return -1; //=> read empty
    char* eod = (char*)memchr(cur, '\0', cursize);
    if (feof(fp) || eod[-1] == '\n') return eod - *pline;
    // line continued
    cursize = *plsize + 1; // last of *pline is '\0'
    if (alloc_line(pline, plsize, *plsize * 2)) return -1;
    cur = *pline + *plsize - cursize;
  }
}

// implement POSIX getline() with fgetc()
static ssize_t
getline2(char** pline, size_t*  plsize, FILE* fp) {
  //if (*plsize < 128) if (alloc_line(pline, plsize, 128)) return -1;
  if (*plsize < 2) if (alloc_line(pline, plsize, 2)) return -1;
  if (*pline == NULL) return -1;
  if (feof(fp)) return -1;
  ssize_t len = 0;
  for (int ch = fgetc(fp); ch != EOF; ch = fgetc(fp)) {
    (*pline)[len++] = ch;
    if ((size_t) len == *plsize) {
      if (alloc_line(pline, plsize, *plsize * 2)) return -1;
    }
    if (ch == '\n') break;
  }
  if (len == 0) return -1;
  (*pline)[len] = '\0';
  return len;
}

enum EntityType : uint16_t {
    ENTITY_TYPE_STATIC_PROP = 0,
    ENTITY_TYPE_PHYSICS_PROP = 1,
    ENTITY_TYPE_LEVEL_GEOMETRY = 2,
    ENTITY_TYPE_PLAYER = 1000,
    ENTITY_TYPE_BOMB = 1001,
    // etc.
};

struct FileHeader {
    uint32_t models_count;
};
// Idea is for each one of these to correspond to one *model load*
// Since we might use the same model many times
struct FileModel {
    uint32_t entity_count;
    uint32_t entities_len;
    char name[16];
};
struct FileEntity {
    EntityType type;
    uint32_t attribs_length;
    std::byte attribs_value[];

    static std::shared_ptr<FileEntity> make_var(size_t extra_bytes) {
        return {
            (FileEntity*) malloc(sizeof(FileEntity)+extra_bytes),
            [](FileEntity* p){free(p);}
        };
    }
};

struct vec2 {
    float x;
    float y;
};
struct vec3 {
    float x;
    float y;
    float z;
};

struct EntStaticPropData {
    vec3 position;
    vec3 rotation;
    vec3 scale;
};

constexpr float radians(float degrees) {
    return 3.14159f * degrees / 180.0f;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("no\n");
        return -1;
    }

    FILE* inp = fopen(argv[1], "rb");
    if (!inp) {
        printf("Couldn't open input file\n");
        return -1;
    }
    size_t len = 0;
    ssize_t nread;
    char* line = nullptr;

    std::map<std::string, std::list<std::shared_ptr<FileEntity>>> models;

    while ((nread = getline1(&line, &len, inp)) != -1) {
        if (strncmp(line, "sp", 2) == 0) {
            char type[8], model_name[16];
            EntStaticPropData attrib;
            sscanf(line, "%7s %15s p %f %f %f r %f %f %f s %f %f %f",
                   type, model_name,
                   &attrib.position.x, &attrib.position.y, &attrib.position.z,
                   &attrib.rotation.x, &attrib.rotation.y, &attrib.rotation.z,
                   &attrib.scale.x, &attrib.scale.y, &attrib.scale.z);

            attrib.rotation.x = radians(attrib.rotation.x);
            attrib.rotation.y = radians(attrib.rotation.y);
            attrib.rotation.z = radians(attrib.rotation.z);

            auto ent_size = sizeof(attrib);
            auto entity = FileEntity::make_var(ent_size);
            entity->type = ENTITY_TYPE_STATIC_PROP;
            entity->attribs_length = ent_size;
            memcpy(entity->attribs_value, &attrib, sizeof(attrib));
            models[model_name].push_back(entity);
        } else if (strncmp(line, "lvgeo", 5) == 0) {
            char type[8], model_name[16];
            sscanf(line, "%7s %15s", type, model_name);
            auto entity = FileEntity::make_var(0);
            entity->type = ENTITY_TYPE_LEVEL_GEOMETRY;
            entity->attribs_length = 0;
            models[model_name].push_back(entity);
        }
    }

    fclose(inp);

    FileHeader fhdr {
        .models_count = models.size(),
    };

    std::vector<FileModel> f_models;
    f_models.reserve(models.size());
    for (auto& [name, entities] : models) {
        FileModel fm;
        fm.entity_count = entities.size();
        fm.entities_len = 0;
        strncpy(fm.name, name.c_str(), sizeof(fm.name)-1);
        for (auto& ent : entities) {
            fm.entities_len += offsetof(typeof(*ent), attribs_value) + ent->attribs_length;
        }
        f_models.push_back(fm);
    }

    FILE* outp = fopen(argv[2], "wb");
    if (!outp) {
        printf("Couldn't open output file\n");
        return -1;
    }

    fwrite(&fhdr, sizeof(fhdr), 1, outp);
    fwrite(f_models.data(), sizeof(f_models[0]), f_models.size(), outp);
    for (auto& [name, entities] : models) {
        for (auto& ent : entities) {
            fwrite(ent.get(), offsetof(typeof(*ent), attribs_value) + ent->attribs_length, 1, outp);
        }
    }

    fclose(outp);
}
