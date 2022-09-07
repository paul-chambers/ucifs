//
// Created by paul on 8/15/22.
//

#define _GNU_SOURCE

#include <stdlib.h>
#include <unistd.h>

#include <ctype.h>
#include <string.h>
#include <time.h>

#include <stdio.h>
#include <fcntl.h>

/* Note: need to build and install the uci project on x86 */
#include <uci.h>

#include <elektra.h>

#include "logStuff.h"
#include "utils.h"
#include "fileHandles.h"
#include "uci2libelektra.h"


typedef struct sSection {
    struct sSection * next;
    const char *      type;
    tHash             hash;
    int               count;
    int               counter;
} tSection;


/********************************/

#ifdef DEBUG
const char * testPaths[] = { "/network", "/system", "/wireless", "/dhcp", NULL };
#endif

/**
 * @brief
 * @param i
 * @return
 */
const char * iterateUCIfiles( int i )
{
    const char * path = testPaths[i];
    logDebug( "  %d: test path = \'%s\'", i, path );
    return path;
}


void dumpKeyMeta( Key * key )
{
    logDebug( "key %s", keyName( key ) );

    KeySet * metaKeys = keyMeta (key);

    for ( elektraCursor it = 0; it < ksGetSize( metaKeys ); ++it )
    {
        Key * meta = ksAtCursor (metaKeys, it);
        logDebug("%s = %s", keyName (meta), keyString (meta));
    }
}

void dumpKeySetMeta( KeySet * keySet )
{
    for ( elektraCursor it = 0; it < ksGetSize( keySet ); ++it )
    {
        Key * meta = ksAtCursor (keySet, it);
        dumpKeyMeta( meta );
    }

}

/**
 * @brief
 * @param keySet
 * @param keyName
 * @param metaKey
 * @param metaValue
 * @return
 */
int setMetadata( KeySet * keyset, const char * keyName, const char * metaKey, const char * metaValue )
{
    Key * key = keyNew( keyName,
                        KEY_META, metaKey, metaValue,
                        KEY_END );
    int result = ksAppendKey( keyset, key );
    if ( result < 0 )
    {
        logError( "failed to append key %s", keyName );
    }
    /* if the append succeeded, the ref count was bumped, so this won't actually delete the key.
     * if the append failed, then delete the key, so the memory it occupies is released */
    keyDel( key );
    return result;
}

/**
 * @brief
 * @param keySet
 * @param keyName
 * @param value
 * @return
 */
int setKeyToInteger( KeySet * keyset, const char * keyName, long value )
{
    logDebug( "%s set to %ld", keyName, value );

    Key * key = keyNew( keyName,
                        KEY_VALUE, &value,
                        KEY_BINARY,
                        KEY_SIZE, sizeof( long ),
                        KEY_META, "type", "long",
                        KEY_END );
    int result = ksAppendKey( keyset, key );
    if ( result < 0 )
    {
        logError( "failed to append key %s", keyName );
    }
    /* if the append succeeded, the ref count was bumped, so this won't actually delete the key.
     * if the append failed, then delete the key, so the memory it occupies is released */
    keyDel( key );
    return result;
}

/**
 * @brief
 * @param keySet
 * @param keyName
 * @param value
 * @return
 */
int setKeyToString( KeySet * keyset, const char * keyName, const char * value, const char * type )
{
    logDebug( "%s set to \'%s\' (%s)", keyName, value, type );
    Key * key = keyNew( keyName,
                        KEY_VALUE, value,
                        KEY_META, "type", type,
                        KEY_END );
    int result = ksAppendKey( keyset, key );
    if ( result < 0 )
    {
        logError( "failed to append key %s", keyName );
    }
    /* if the append succeeded, the ref count was bumped, so this won't actually delete the key.
     * if the append failed, then delete the key, so the memory it occupies is released */
    keyDel( key );
    return result;
}

/**
 * @brief
 * @param keySet
 * @param keyName
 * @param value
 * @return
 */
int setKeyToMAC( KeySet * keyset, const char * keyName, const char * value )
{
    return setKeyToString( keyset, keyName, value, "macaddr" );
}

/**
 * @brief
 * @param keySet
 * @param keyName
 * @param value
 * @return
 */
int setKeyToIPv4( KeySet * keyset, const char * keyName, const char * value )
{
    return setKeyToString( keyset, keyName, value, "ipv4addr" );
}

/**
 * @brief
 * @param keySet
 * @param keyName
 * @param value
 * @return
 */
int setKeyToIPv6( KeySet * keyset, const char * keyName, const char * value )
{
    return setKeyToString( keyset, keyName, value, "ipv6addr" );
}

/**
 * @brief generic 'set a key to a value'
 * it also figures out what type of value is being set, and calls the right
 * routine to set it, which sets the 'type' metadata.
 * @param keySet
 * @param keyName
 * @param value
 * @return
 */
int setKey( KeySet * keyset, const char * keyName, const char * value )
{
    int result;
    const char * p = value;

    /* ToDo: tighten up the format checks - this is way too 'loose' */
    /* ToDo: add support for netmask */
    int periodCount = 0;
    int colonCount  = 0;
    int slashCount  = 0;
    bool isDecimal  = true;
    bool isHex      = true;
    for ( p = value; *p != '\0'; ++p )
    {
        switch (*p)
        {
        case '.': ++periodCount; break;
        case ':': ++colonCount;  break;
        case '/': ++slashCount;  break;
        default:
            if ( !isdigit(*p) ) isDecimal = false;
            if ( !isxdigit(*p) ) isHex = false;
            break;
        }
    }

    if ( isHex && colonCount == 5 && periodCount == 0 && slashCount == 0 )
    {
        result = setKeyToMAC( keyset, keyName, value );
    }
    else if ( isHex && colonCount > 2 )
    {
        result = setKeyToIPv6( keyset, keyName, value );
    }
    else if ( isDecimal )
    {
        if (periodCount == 0)
            result = setKeyToInteger( keyset, keyName, atol( value ));
        else if (periodCount == 3)
            result = setKeyToIPv4( keyset, keyName, value );
        else
            result = setKeyToString( keyset, keyName, value, "string" );
    }
    else
        result = setKeyToString( keyset, keyName, value, "string" );

    return result;
}

/**
 * @brief
 * @param keySet
 * @param keyName
 * @return
 */
int createList( KeySet * keyset, const char * keyName )
{
    logDebug( "%s set as list", keyName );
    Key * key = keyNew( keyName,
                        KEY_VALUE, "",
                        KEY_META, "type", "list",
                        KEY_META, "array", "",
                        KEY_END );
    int result = ksAppendKey( keyset, key );
    if ( result < 0 )
    {
        logError( "failed to append key %s", keyName );
    }
    /* if the append succeeded, the ref count was bumped, so this won't actually delete the key.
     * if the append failed, then delete the key, so the memory it occupies is released */
    keyDel( key );
    return result;
}

/**
 * @brief
 * @param keySet
 * @param keyName
 * @param section
 * @return
 */
char * storeSection( KeySet * keySet, char * keyName, const struct uci_section * section )
{
    /* ToDo: establish the section in libelektra (i.e. check it exists, create it if not) */
    logDebug( "%s section with type \'%s\'", keyName, section->type );
    setMetadata(keySet, keyName, "type", section->type );

    struct uci_element * optionElement;
    uci_foreach_element( &section->options, optionElement )
    {
        struct uci_option * option = uci_to_option( optionElement );
        switch ( option->type )
        {
        case UCI_TYPE_STRING:
            keyName = appendKeyName( keyName, option->e.name );

            setKey( keySet, keyName, option->v.string );
            break;

        case UCI_TYPE_LIST:
            {
                keyName = appendKeyName( keyName, option->e.name );
                createList( keySet, keyName );

                int index = 0;
                char indexStr[32];
                indexStr[0] = '\0';

                struct uci_element * listElement;
                uci_foreach_element( &option->v.list, listElement )
                {
                    snprintf( indexStr, sizeof(indexStr), "#%03d", index++ );
                    keyName = appendKeyName( keyName, indexStr );

                    setKey( keySet, keyName, listElement->name );

                    keyName = trimKey( keyName );
                }
                /* 'array' metadata value is set to the name of the last list element by convention */
                setMetadata( keySet, keyName, "array", indexStr );
            }
            break;

        default:
            keyName = appendKeyName( keyName, "<error>" );
            logError( "option '%s' has an unknown type (%d)",
                      option->e.name,
                      option->type );
            break;
        }
        keyName = trimKey( keyName );
    }
    return keyName;
}

tSection * buildAnonSectionList( const struct uci_package * packageElement )
{
    tSection * result = NULL;
    struct uci_element * sectionElement;

    /* Scan the list of sections, looking for anonymous ones */
    uci_foreach_element( &packageElement->sections, sectionElement )
    {
        struct uci_section * section = uci_to_section( sectionElement );
        if ( section->anonymous ) {
            /* if it's anonymous, then it does not have a unique name, only a type. So we pre-scan the
             * list of sections to build a list of the types of anonymous sections, */
            tSection * s;
            tHash typeHash = hashString( section->type );
            for ( s = result; s != NULL; s = s->next )
            {
                if ( s->hash == typeHash )
                {
                    /* found it, so bump up the count */
                    s->count++;
                    break;
                }
            }
            if ( s == NULL )
            {
                /* first time we've seen this section type, so add it to the list */
                s = calloc( 1, sizeof( tSection ) );
                if ( s != NULL )
                {
                    s->type  = strdup( section->type );
                    s->hash  = typeHash;
                    s->count = 1;
                    s->next  = result;
                    result = s;
                }
            }
        }
    }
    return result;
}

void freeAnonSectionList( tSection * anonSections )
{/* clean up the anonSection list */
    for ( tSection * s = anonSections; s != NULL; )
    {
        tSection * next = s->next;
        free( (void *)s->type );
        free( s );
        s = next;
    }
}

/* look for the matching section type in the list created by buildAnonSectionList()
 * to determine if the type is  ambiguous (i.e. there is more than one anonymous
 * section with this type in this package)
 *
 * Note: the counter in the entry for this type is incremented as a side-effect */

bool isAmbiguous( tSection * anonSections, const char * type, int * counter )
{
    bool result = false;

    tHash typeHash = hashString( type );
    tSection * s = anonSections;
    while ( s != NULL )
    {
        if ( s->hash == typeHash )
        {
            /* found it. is there more than one of them? */
            if ( s->count > 1 )
            {
                result = true;
                /* we need this later to disambiguate the anonymous sections with the same type */
                (*counter) = s->counter++;
            }
            break;
        }
        s = s->next;
    }
    return result;
}

/**
 * @brief mirror the imported UCI structures into libelektra
 * @param ctx
 */
void uci2elektra( const struct uci_context * ctx )
{
    char * keyName = strdup( "system:/config" );

    /* ToDo: establish the config root in libelektra (i.e. check it exists, create it if not) */
    Key * parent = keyNew(keyName, KEY_END);
    KDB * kdb = kdbOpen( NULL, parent );
    logDebug( "kdb = %p for \'%s\'", kdb, keyName );
    if ( kdb == NULL )
    {
        logError( "unable to open libelektra" );
        return;
    }
    KeySet * keySet = ksNew(0, KS_END);
    if ( keySet == NULL )
    {
        logError( "failed to create a KeySet" );
        return;
    }

    /* It's necessary to preload the keySet. No idea why, but errors occur if you don't. */
    kdbGet (kdb, keySet, parent);

    struct uci_element * rootElement;
    tSection * anonSections = NULL;

    setMetadata( keySet, keyName, "type", "config" );

    uci_foreach_element( &ctx->root, rootElement )
    {
        struct uci_package * packageElement = uci_to_package( rootElement );

        keyName = appendKeyName( keyName, packageElement->e.name );
        /* ToDo: establish the package (i.e. check it exists, create it if not) */
        logDebug( "establish package \'%s\'", keyName );

        /* build a list of the types of the anonymopus sections in this
         * package and the number of times each one appears */
        anonSections = buildAnonSectionList( packageElement );

#ifdef DEBUG
        for ( tSection * s = anonSections; s != NULL; s = s->next  )
        {
            logDebug( "anon section - type: \'%s\' (hash: 0x%lx) count: %d", s->type, s->hash, s->count );
        }
#endif

        struct uci_element * sectionElement;
        uci_foreach_element( &packageElement->sections, sectionElement )
        {
            struct uci_section * section = uci_to_section( sectionElement );

            logDebug( "section type: %s name: %s", section->type, section->e.name );
            /* The list of anonymous types we built earlier can tell us if there's more than
             * one anonymous section with the same type. If so, we set ambiguous = true. */
            bool ambiguous = false;
            int counter; /* only referenced when ambiguous == true */

            if ( section->anonymous ) {
                /* there's no e.name, so use the type instead */
                keyName = appendKeyName( keyName, section->type );
                ambiguous = isAmbiguous( anonSections, section->type, &counter );
            } else {
                keyName = appendKeyName( keyName, section->e.name );
            }

            if ( ambiguous ) {
                /* there is more than one anonymous section with the same type. So append an index to
                 * the key to produce /{type}/{index} so they can co-exist within the same package */
                logDebug( "section: \'%s[%d]\'", section->type, counter );
                char indexStr[32];
                snprintf( indexStr, sizeof( indexStr ), "#%03d", counter );
                keyName = appendKeyName( keyName, indexStr );
            }

            keyName = storeSection( keySet, keyName, section );

            if ( ambiguous )
            {
                /* remove the additional index used to disambiguate */
                keyName = trimKey( keyName );
            }
            keyName = trimKey( keyName );
        }
        keyName = trimKey( keyName );
    }
    int result;
    result = kdbSet( kdb , keySet, parent );
    if ( result != 0)
    {
        logDebug( "kdbSet returned %d", result );
        dumpKeySetMeta( keySet );
    }

    result = kdbClose( kdb, parent );
    logDebug( "kdbClose returned %d", result );

    freeAnonSectionList( anonSections );
}

