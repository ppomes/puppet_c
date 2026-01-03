/**
 * @file puppet_loader.h
 * @brief Module and manifest loading system for Puppet
 *
 * This module provides functionality for loading Puppet manifests following
 * the standard Puppet directory structure:
 * - site.pp in manifests/
 * - Modules under modules/
 * - Class::name resolution to modules/class/manifests/name.pp
 * - Support for autoloading classes via include statements
 */

#ifndef PUPPET_LOADER_H
#define PUPPET_LOADER_H

#include "puppet_ast.h"
#include "puppet_interpreter.h"
#include <stdbool.h>
#include <pthread.h>

/**
 * @brief Module loader context
 * 
 * Maintains state for the module loading system including search paths,
 * loaded modules cache, and parsing context.
 */
typedef struct puppet_loader {
    char *base_path;           /**< Base directory for Puppet code */
    char *modules_path;        /**< Path to modules directory */
    char *manifests_path;      /**< Path to manifests directory */

    /* Cache of loaded classes to avoid reloading */
    struct {
        char **class_names;
        puppet_stmt_t **class_defs;
        size_t count;
        size_t capacity;
    } loaded_classes;

    /* Cache of custom function lookups */
    struct {
        char **func_names;     /**< Function names checked */
        bool *func_exists;     /**< Whether function exists */
        size_t count;
        size_t capacity;
    } custom_functions;

    /* Cache of parsed manifest files to avoid re-parsing */
    struct {
        char **file_paths;     /**< Absolute file paths */
        puppet_program_t **programs;  /**< Parsed programs */
        size_t count;
        size_t capacity;
    } parsed_manifests;

    /* Thread synchronization for parallel node processing */
    pthread_mutex_t cache_mutex;   /**< Protects all caches */

    /* Current parsing context */
    puppet_env_t *env;         /**< Environment for evaluation */
} puppet_loader_t;

/**
 * @brief Create a new module loader
 * @param base_path Base directory containing modules/ and manifests/
 * @return New loader instance or NULL on error
 */
puppet_loader_t *puppet_loader_create(const char *base_path);

/**
 * @brief Destroy a module loader
 * @param loader Loader to destroy
 */
void puppet_loader_destroy(puppet_loader_t *loader);

/**
 * @brief Resolve a class name to a file path
 * 
 * Converts class names like "apache::vhost" to paths like
 * "modules/apache/manifests/vhost.pp"
 * 
 * @param loader Module loader context
 * @param class_name Class name to resolve (e.g., "apache::vhost")
 * @param path_buffer Buffer to store the resolved path
 * @param buffer_size Size of the path buffer
 * @return true if path was resolved successfully
 */
bool puppet_loader_resolve_class_path(puppet_loader_t *loader,
                                      const char *class_name,
                                      char *path_buffer,
                                      size_t buffer_size);

/**
 * @brief Load a class by name
 * 
 * Loads and parses a class definition from the module system.
 * Classes are cached to avoid reloading.
 * 
 * @param loader Module loader context
 * @param class_name Name of the class to load
 * @return Parsed class statement or NULL on error
 */
puppet_stmt_t *puppet_loader_load_class(puppet_loader_t *loader,
                                        const char *class_name);

/**
 * @brief Load a defined type by name
 *
 * Loads and parses a defined type from the module system.
 * Uses the same path resolution as classes (module::name → module/manifests/name.pp)
 *
 * @param loader Module loader context
 * @param define_name Name of the defined type to load
 * @return Parsed define statement or NULL on error
 */
puppet_stmt_t *puppet_loader_load_define(puppet_loader_t *loader,
                                         const char *define_name);

/**
 * @brief Load and parse a manifest file
 * @param loader Module loader context
 * @param file_path Path to the manifest file
 * @return Parsed program or NULL on error
 */
puppet_program_t *puppet_loader_load_manifest(puppet_loader_t *loader,
                                              const char *file_path);

/**
 * @brief Load the site.pp manifest
 * 
 * Loads the main site manifest from manifests/site.pp
 * 
 * @param loader Module loader context
 * @return Parsed program or NULL if not found
 */
puppet_program_t *puppet_loader_load_site(puppet_loader_t *loader);

/**
 * @brief Execute an include statement
 * 
 * Loads and executes the specified class, handling module autoloading
 * 
 * @param loader Module loader context
 * @param class_name Name of the class to include
 * @param env Environment for execution
 * @return true if successful
 */
bool puppet_loader_include_class(puppet_loader_t *loader,
                                 const char *class_name,
                                 puppet_env_t *env);

/**
 * @brief Check if a class has been loaded
 * @param loader Module loader context
 * @param class_name Name of the class to check
 * @return true if the class has been loaded
 */
bool puppet_loader_is_class_loaded(puppet_loader_t *loader,
                                   const char *class_name);

/**
 * @brief Set the module search path
 * @param loader Module loader context
 * @param modules_path Path to modules directory
 */
void puppet_loader_set_modules_path(puppet_loader_t *loader,
                                    const char *modules_path);

/**
 * @brief Set the manifests search path
 * @param loader Module loader context
 * @param manifests_path Path to manifests directory
 */
void puppet_loader_set_manifests_path(puppet_loader_t *loader,
                                      const char *manifests_path);

/**
 * @brief Check if a custom function exists in any module
 *
 * Searches for function_name.rb in module lib directories:
 * - modules/MODULE/lib/puppet/parser/functions/ (Puppet 3 style)
 * - modules/MODULE/lib/puppet/functions/ (Puppet 4+ style)
 *
 * @param loader Module loader context
 * @param func_name Name of the function to find
 * @return true if the function exists as a custom Ruby function
 */
bool puppet_loader_has_custom_function(puppet_loader_t *loader,
                                       const char *func_name);

#endif /* PUPPET_LOADER_H */