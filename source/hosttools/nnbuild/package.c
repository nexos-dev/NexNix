/*
    package.c - handles build process
    Copyright 2021 - 2022 The NexNix Project

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

         http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

/// @file package.c

#include "nnbuild.h"
#include <conf.h>
#include <libnex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Contains all packages and groups
static package_t* packages = NULL;
static package_t* curPackage = NULL;

static packageGroup_t* pkgGroups = NULL;
static packageGroup_t* curGroup = NULL;

// What we are expecting. 1 = package, 2 = group
static int expecting = 0;

#define EXPECTING_PACKAGE 1
#define EXPECTING_GROUP   2

// The current property name
static char* prop = NULL;

// The current line number
static int lineNo = 0;

// Data type union
union val
{
    int64_t numVal;
    char strVal[BLOCK_BUFSZ * 4];
};

// Adds a property to the current block
int addProperty (char* prop, union val* val, int isStart, int dataType);

// Adds a package to the package list
int addPackage (char* name);

// Adds a package group to the list
int addGroup (char* name);

// Adds a command to a package spec
int addCommand (char* action, char* command);

// Finds a package in the list
package_t* findPackage (char* pkg);

// Finds a package group
packageGroup_t* findGroup (char* groupName);

// Deletes package tree
void freePackageTree()
{
    // Free every package group
    packageGroup_t* curGrp = pkgGroups;
    while (curGrp)
    {
        // Free every dependency
        dependency_t* iter = curGrp->packages;
        while (iter)
        {
            dependency_t* oldDep = iter;
            iter = iter->next;
            free (oldDep);
        }
        // Free subrgroups
        dependencyGroup_t* grpIter = curGrp->subGroups;
        while (grpIter)
        {
            dependencyGroup_t* oldGrp = grpIter;
            grpIter = grpIter->next;
            free (oldGrp);
        }
        packageGroup_t* oldGrp = curGrp;
        curGrp = curGrp->next;
        free (oldGrp);
    }
    // Free every package and dependency
    package_t* curPkg = packages;
    while (curPkg)
    {
        // Free the dependency list
        dependency_t* curDep = curPkg->depends;
        while (curDep)
        {
            dependency_t* oldDep = curDep;
            curDep = curDep->next;
            free (oldDep);
        }
        package_t* oldPkg = curPkg;
        curPkg = curPkg->next;
        free (oldPkg);
    }
}

// Adds a package to the list
int addPackage (char* name)
{
    // Create the package
    package_t* newPkg = (package_t*) calloc_s (sizeof (package_t));
    // Add it to the list
    if (!curPackage)
    {
        // The first one
        packages = newPkg;
        curPackage = newPkg;
    }
    else
    {
        curPackage->next = newPkg;
        curPackage = newPkg;
    }
    // Set the name of this package
    newPkg->name = name;
    expecting = EXPECTING_PACKAGE;
    return 1;
}

// Adds a group to the list
int addGroup (char* name)
{
    // Create the new group
    packageGroup_t* newGrp = (packageGroup_t*) calloc_s (sizeof (packageGroup_t));
    // Add it to the list
    if (!curGroup)
    {
        // The first one
        pkgGroups = newGrp;
        curGroup = newGrp;
    }
    else
    {
        curGroup->next = newGrp;
        curGroup = newGrp;
    }
    // Set the name of this group
    newGrp->name = name;
    expecting = EXPECTING_GROUP;
    return 1;
}

// Finds a package in the list
package_t* findPackage (char* pkg)
{
    // Compare names until it is found
    package_t* iter = packages;
    while (strcmp (iter->name, pkg) != 0)
    {
        iter = iter->next;
        if (!iter)
            return NULL;
    }
    return iter;
}

// Finds a package group
packageGroup_t* findGroup (char* groupName)
{
    // Compare names
    packageGroup_t* iter = pkgGroups;
    while (strcmp (iter->name, groupName) != 0)
    {
        iter = iter->next;
        if (!iter)
            return NULL;
    }
    return iter;
}

// Adds a command to a package
int addCommand (char* action, char* command)
{
    // Figure out what the action is
    if (!strcmp (action, "download"))
    {
        if ((strlcpy (curPackage->downloadAction, command, ACTION_BUFSIZE) >= ACTION_BUFSIZE))
            error (_ ("%s:%d: string overflow"), ConfGetFileName(), lineNo);
    }
    else if (!strcmp (action, "configure"))
    {
        if ((strlcpy (curPackage->configureAction, command, ACTION_BUFSIZE) >= ACTION_BUFSIZE))
            error (_ ("%s:%d: string overflow"), ConfGetFileName(), lineNo);
    }
    else if (!strcmp (action, "build"))
    {
        if ((strlcpy (curPackage->buildAction, command, ACTION_BUFSIZE) >= ACTION_BUFSIZE))
            error (_ ("%s:%d: string overflow"), ConfGetFileName(), lineNo);
    }
    else if (!strcmp (action, "install"))
    {
        if ((strlcpy (curPackage->installAction, command, ACTION_BUFSIZE) >= ACTION_BUFSIZE))
            error (_ ("%s:%d: string overflow"), ConfGetFileName(), lineNo);
    }
    else if (!strcmp (action, "clean"))
    {
        if ((strlcpy (curPackage->cleanAction, command, ACTION_BUFSIZE) >= ACTION_BUFSIZE))
            error (_ ("%s:%d: string overflow"), ConfGetFileName(), lineNo);
    }
    else
        return 0;
    return 1;
}

// Adds a package to a package group
static int addPackageToGroup (char* packageName)
{
    if (expecting != EXPECTING_GROUP)
    {
        error (_ ("package list unexpected"));
        return 0;
    }
    // Find it in the list
    package_t* package = findPackage (packageName);
    if (!package)
    {
        error (_ ("%s:%d: package \"%s\" undeclared"), ConfGetFileName(), lineNo, packageName);
        return 0;
    }
    // Store it
    dependency_t* newDep = (dependency_t*) malloc_s (sizeof (dependency_t));
    // Add it to the list in the package group
    newDep->next = curGroup->packages;
    curGroup->packages = newDep;
    // Store the package
    newDep->package = package;
    return 1;
}

// Adds a dependency to a package
static int addDependencyToPackage (char* depName)
{
    // Find the package
    package_t* package = findPackage (depName);
    if (!package)
    {
        error (_ ("%s:%d: package \"%s\" undeclared"), ConfGetFileName(), lineNo, depName);
        return 0;
    }
    // Create the dependency
    dependency_t* dep = (dependency_t*) malloc_s (sizeof (dependency_t));
    dep->package = package;
    // Add it to the list
    dep->next = curPackage->depends;
    curPackage->depends = dep;
    return 1;
}

// Adds a package group dependency to a package group
int addGroupToGroup (char* groupName)
{
    // Find the group to be added
    packageGroup_t* group = findGroup (groupName);
    if (!group)
    {
        error (_ ("%s:%d: package group \"%s\" undeclared"), ConfGetFileName(), lineNo, groupName);
        return 0;
    }
    // Allocate the dependency group
    dependencyGroup_t* depGroup = (dependencyGroup_t*) malloc_s (sizeof (dependencyGroup_t));
    // Set the group and add it to the list
    depGroup->group = group;
    depGroup->next = curGroup->subGroups;
    curGroup->subGroups = depGroup;
    return 1;
}

// Adds a property
int addProperty (char* newProp, union val* val, int isStart, int dataType)
{
    // Check if the name needs to be reset
    if (isStart)
    {
        prop = newProp;
        return 1;
    }
    // Figure what this is
    if (expecting == EXPECTING_PACKAGE)
    {
        // Check if this is a dependency
        if (!strcmp (prop, "dependencies"))
        {
            // Check data type
            if (dataType != DATATYPE_STRING)
            {
                error (_ ("%s:%d: property \"dependencies\" requires a string value"), ConfGetFileName(), lineNo);
                return 0;
            }
            // Check that this isn't a number
            if (!addDependencyToPackage (val->strVal))
                return 0;
        }
        // Check if this is a bindinstall prop
        if (!strcmp (prop, "bindinstall"))
        {
            if (dataType != DATATYPE_NUMBER)
            {
                error (_ ("%s:%d: property \"bindinstall\" requires a numeric value"), ConfGetFileName(), lineNo);
                return 0;
            }
            curPackage->bindInstall = true;
        }
        else
        {
            // This could be a command spec
            if (!addCommand (prop, val->strVal))
                goto invalidProp;
        }
    }
    else if (expecting == EXPECTING_GROUP)
    {
        // Check if this is a list of packages in the group
        if (!strcmp (prop, "packages"))
        {
            // Check data type
            if (dataType != DATATYPE_STRING)
            {
                error (_ ("%s:%d: property \"package\" requires a string value"), ConfGetFileName(), lineNo);
                return 0;
            }
            if (!addPackageToGroup (val->strVal))
                return 0;
        }
        // Check if this is a sub group
        else if (!strcmp (prop, "subgroups"))
        {
            // Check data type
            if (dataType != DATATYPE_STRING)
            {
                error (_ ("%s:%d: property \"subgroups\" requires a string value"), ConfGetFileName(), lineNo);
                return 0;
            }
            if (!addGroupToGroup (val->strVal))
                return 0;
        }
        else
            goto invalidProp;
    }
    return 1;
invalidProp:
    error (_ ("%s:%d: invalid property %s"), ConfGetFileName(), lineNo, prop);
    return 0;
}

// Handles the build process
int buildPackages (int groupOrPkg, char* name, char* action)
{
    // Check if we need to build a group or a package
    if (!groupOrPkg)
    {
        // All is handled specially
        if (strcmp (name, "all") != 0)
        {
            // Find the group and build itJ
            packageGroup_t* group = findGroup (name);
            if (!group)
            {
                error (_ ("package group %s doesn't exist"), name);
                return 0;
            }
            return buildGroup (group, action);
        }
        else
        {
            // Build every package
            package_t* curPkg = packages;
            while (curPkg)
            {
                if (!buildPackage (curPkg, action))
                    return 0;
                curPkg = curPkg->next;
            }
            return 1;
        }
    }
    else
    {
        // Build one package
        package_t* pkg = findPackage (name);
        if (!pkg)
        {
            error (_ ("package %s doesn't exist"), name);
            return 0;
        }
        return buildPackage (pkg, action);
    }
}

// Converts the parse tree into the package tree
int buildPackageTree (ListHead_t* head)
{
    // Iterate through the parse tree
    ListEntry_t* iter = ListFront (head);
    while (iter)
    {
        ConfBlock_t* curBlock = ListEntryData (iter);
        // Set diagnostic line number
        lineNo = curBlock->lineNo;
        // Figure out what to do here
        if (!strcmp (curBlock->blockType, "package"))
        {
            // Check that a name was given
            if (curBlock->blockName[0] == '\0')
            {
                error ("%s:%d: package declaration requires name", ConfGetFileName(), lineNo);
                return 0;
            }
            // Add it
            if (!addPackage (curBlock->blockName))
                return 0;
        }
        else if (!strcmp (curBlock->blockType, "group"))
        {
            // Check that a name was given
            if (curBlock->blockName[0] == '\0')
            {
                error ("%s:%d: package group declaration requires name", ConfGetFileName(), lineNo);
                return 0;
            }
            // Add it
            if (!addGroup (curBlock->blockName))
                return 0;
        }
        else
        {
            // Invalid block
            error ("%s:%d: invalid block type specified", ConfGetFileName(), lineNo);
            return 0;
        }
        // Apply the properties
        ListHead_t* propsList = curBlock->props;
        ListEntry_t* propEntry = ListFront (propsList);
        while (propEntry)
        {
            ConfProperty_t* curProp = ListEntryData (propEntry);
            lineNo = curProp->lineNo;
            // Start a new property
            if (!addProperty (curProp->name, NULL, 1, 0))
                return 0;
            // Add all the actual values
            for (int i = 0; i < curProp->nextVal; ++i)
            {
                lineNo = curProp->lineNo;
                // Declare value union
                union val val;
                if (curProp->vals[i].type == DATATYPE_IDENTIFIER)
                    strcpy (val.strVal, curProp->vals[i].id);
                else if (curProp->vals[i].type == DATATYPE_STRING)
                {
                    mbstate_t mbState = {0};
                    c32stombs (val.strVal, curProp->vals[i].str, (size_t) BLOCK_BUFSZ * 4, &mbState);
                }
                else
                    val.numVal = curProp->vals[i].numVal;
                if (!addProperty (NULL, &val, 0, curProp->vals[i].type))
                    return 0;
            }
            propEntry = ListIterate (propEntry);
        }
        iter = ListIterate (iter);
    }
    return 1;
}
