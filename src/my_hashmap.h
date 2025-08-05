#pragma once

typedef struct _hash_node {
    char* key;
    void* value;
    struct _hash_node* next;
} _hash_node;

typedef struct HashMap {
    _hash_node** table;
    int capacity;
    int size;
} HashMap;

typedef void (*_hashmap_free_func)(void*);


unsigned int _hashmap_hash(const char* str, int capacity);
HashMap* hashmap_create(int initialSize);
void hashmap_clear(HashMap* map, _hashmap_free_func free_func);
void hashmap_free(HashMap* map, _hashmap_free_func free_func);
void hashmap_resize(HashMap* map);
void* hashmap_insert(HashMap* map, const char* key, void* value);
void* hashmap_find(HashMap* map, const char* key);
void* hashmap_remove(HashMap* map, const char* key);