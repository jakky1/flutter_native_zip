#include "my_hashmap.h"
#include "my_common.h"

#include <string.h>
#include <stdlib.h>
#include <stddef.h> // size_t, NULL

#define INITIAL_SIZE 16
#define LOAD_FACTOR 0.75


unsigned int _hashmap_hash(const char* str, int capacity) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;
    return hash % capacity;
}

HashMap* hashmap_create(int initialSize) {
    HashMap* map = (HashMap*) malloc(sizeof(HashMap));
    map->capacity = initialSize;
    map->size = 0;
    map->table = (_hash_node**) calloc(map->capacity, sizeof(_hash_node*));
    return map;
}

void hashmap_clear(HashMap* map, _hashmap_free_func free_func) {
    if (map->size == 0) return;
    for (int i = 0; i < map->capacity; i++) {
        _hash_node* node = map->table[i];
        while (node) {
            if (free_func) free_func(node->value);
            _hash_node* tmp = node;
            node = node->next;
            free(tmp->key);
            free(tmp);
        }
        map->table[i] = NULL;
    }
    map->size = 0;
}

void hashmap_free(HashMap* map, _hashmap_free_func free_func) {
    hashmap_clear(map, free_func);
    free(map->table);
    free(map);
}

void hashmap_resize(HashMap* map) {
    int old_capacity = map->capacity;
    map->capacity *= 2;
    _hash_node** new_table = (_hash_node**) calloc(map->capacity, sizeof(_hash_node*));

    for (int i = 0; i < old_capacity; i++) {
        _hash_node* node = map->table[i];
        while (node) {
            _hash_node* next = node->next;
            unsigned int idx = _hashmap_hash(node->key, map->capacity);
            node->next = new_table[idx];
            new_table[idx] = node;
            node = next;
        }
    }
    free(map->table);
    map->table = new_table;
}

void* hashmap_insert(HashMap* map, const char* key, void* value) {
    if ((float)(map->size + 1) / map->capacity > LOAD_FACTOR) {
        hashmap_resize(map);
    }
    unsigned int idx = _hashmap_hash(key, map->capacity);
    _hash_node* node = map->table[idx];
    while (node) {
        if (strcmp(node->key, key) == 0) {
            void* ret = node->value;
            node->value = value;
            return ret;
        }
        node = node->next;
    }
    node = (_hash_node*) malloc(sizeof(_hash_node));
    node->key = strdup(key);
    node->value = value;
    node->next = map->table[idx];
    map->table[idx] = node;
    map->size++;
    return NULL;
}

void* hashmap_find(HashMap* map, const char* key) {
    unsigned int idx = _hashmap_hash(key, map->capacity);
    _hash_node* node = map->table[idx];
    while (node) {
        if (strcmp(node->key, key) == 0) {
            return node->value;
        }
        node = node->next;
    }
    return NULL;
}

void* hashmap_remove(HashMap* map, const char* key) {
    unsigned int idx = _hashmap_hash(key, map->capacity);
    _hash_node* node = map->table[idx], * prev = NULL;
    while (node) {
        if (strcmp(node->key, key) == 0) {
            if (prev)
                prev->next = node->next;
            else
                map->table[idx] = node->next;
            void *ret = node->value;
            free(node->key);
            free(node);
            map->size--;
            return ret;
        }
        prev = node;
        node = node->next;
    }
    return NULL;
}
