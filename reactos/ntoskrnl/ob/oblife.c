/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/ob/create.c
 * PURPOSE:         Manages the lifetime of an Object, including its creation,
 *                  and deletion, as well as setting or querying any of its
 *                  information while it is active. Since Object Types are also
 *                  Objects, those are also managed here.
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 *                  Eric Kohl
 *                  Thomas Weidenmueller (w3seek@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <internal/debug.h>

extern ULONG NtGlobalFlag;

POBJECT_TYPE ObTypeObjectType = NULL;
KEVENT ObpDefaultObject;
WORK_QUEUE_ITEM ObpReaperWorkItem;
volatile PVOID ObpReaperList;

/* PRIVATE FUNCTIONS *********************************************************/

VOID
FASTCALL
ObpDeallocateObject(IN PVOID Object)
{
    PVOID HeaderLocation;
    POBJECT_HEADER Header;
    POBJECT_TYPE ObjectType;
    POBJECT_HEADER_HANDLE_INFO HandleInfo;
    POBJECT_HEADER_NAME_INFO NameInfo;
    POBJECT_HEADER_CREATOR_INFO CreatorInfo;
    PAGED_CODE();

    /* Get the header and assume this is what we'll free */
    Header = OBJECT_TO_OBJECT_HEADER(Object);
    ObjectType = Header->Type;
    HeaderLocation = Header;

    /* To find the header, walk backwards from how we allocated */
    if ((CreatorInfo = OBJECT_HEADER_TO_CREATOR_INFO(Header)))
    {
        HeaderLocation = CreatorInfo;
    }
    if ((NameInfo = OBJECT_HEADER_TO_NAME_INFO(Header)))
    {
        HeaderLocation = NameInfo;
    }
    if ((HandleInfo = OBJECT_HEADER_TO_HANDLE_INFO(Header)))
    {
        HeaderLocation = HandleInfo;
    }

    /* Check if we have create info */
    if (Header->Flags & OB_FLAG_CREATE_INFO)
    {
        /* Double-check that it exists */
        if (Header->ObjectCreateInfo)
        {
            /* Free it */
            ObpReleaseCapturedAttributes(Header->ObjectCreateInfo);
            Header->ObjectCreateInfo = NULL;
        }
    }

    /* Check if a handle database was active */
    if ((HandleInfo) && (Header->Flags & OB_FLAG_SINGLE_PROCESS))
    {
        /* Free it */
        ExFreePool(HandleInfo->HandleCountDatabase);
        HandleInfo->HandleCountDatabase = NULL;
    }

    /* Check if we have a name */
    if ((NameInfo) && (NameInfo->Name.Buffer))
    {
        /* Free it */
        ExFreePool(NameInfo->Name.Buffer);
        NameInfo->Name.Buffer = NULL;
    }

    /* Free the object using the same allocation tag */
    ExFreePoolWithTag(HeaderLocation,
                      ObjectType ? TAG('T', 'j', 'b', 'O') : ObjectType->Key);

    /* Decrease the total */
    ObjectType->TotalNumberOfObjects--;
}

VOID
FASTCALL
ObpDeleteObject(IN PVOID Object)
{
    POBJECT_HEADER Header;
    POBJECT_TYPE ObjectType;
    POBJECT_HEADER_NAME_INFO NameInfo;
    POBJECT_HEADER_CREATOR_INFO CreatorInfo;
    PAGED_CODE();

    /* Get the header and type */
    Header = OBJECT_TO_OBJECT_HEADER(Object);
    ObjectType = Header->Type;

    /* Get creator and name information */
    NameInfo = OBJECT_HEADER_TO_NAME_INFO(Header);
    CreatorInfo = OBJECT_HEADER_TO_CREATOR_INFO(Header);

    /* Check if the object is on a type list */
    if ((CreatorInfo) && !(IsListEmpty(&CreatorInfo->TypeList)))
    {
        /* Remove the object from the type list */
        RemoveEntryList(&CreatorInfo->TypeList);
    }

    /* Check if we have a name */
    if ((NameInfo) && (NameInfo->Name.Buffer))
    {
        /* Free it */
        ExFreePool(NameInfo->Name.Buffer);

        /* Clean up the string so we don't try this again */
        RtlInitUnicodeString(&NameInfo->Name, NULL);
    }

    /* Check if we have a security descriptor */
    if (Header->SecurityDescriptor)
    {
        ObjectType->TypeInfo.SecurityProcedure(Object,
                                               DeleteSecurityDescriptor,
                                               0,
                                               NULL,
                                               NULL,
                                               &Header->SecurityDescriptor,
                                               0,
                                               NULL);
    }

    /* Check if we have a delete procedure */
    if (ObjectType->TypeInfo.DeleteProcedure)
    {
        /* Call it */
        ObjectType->TypeInfo.DeleteProcedure(Object);
    }

    /* Now de-allocate all object members */
    ObpDeallocateObject(Object);
}

VOID
NTAPI
ObpReapObject(IN PVOID Parameter)
{
    POBJECT_HEADER ReapObject;
    PVOID NextObject;

    /* Start reaping */
    while((ReapObject = InterlockedExchangePointer(&ObpReaperList, NULL)))
    {
        /* Start deletion loop */
        do
        {
            /* Get the next object */
            NextObject = ReapObject->NextToFree;

            /* Delete the object */
            ObpDeleteObject(&ReapObject->Body);

            /* Move to the next one */
            ReapObject = NextObject;
        } while(NextObject != NULL);
    }
}

NTSTATUS
STDCALL
ObpCaptureObjectName(IN OUT PUNICODE_STRING CapturedName,
                     IN PUNICODE_STRING ObjectName,
                     IN KPROCESSOR_MODE AccessMode)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG StringLength;
    PWCHAR StringBuffer = NULL;
    UNICODE_STRING LocalName = {}; /* <= GCC 4.0 + Optimizer */
    
    /* Initialize the Input String */
    RtlInitUnicodeString(CapturedName, NULL);

    /* Protect everything */
    _SEH_TRY
    {
        /* First Probe the String */
        DPRINT("ObpCaptureObjectName: %wZ\n", ObjectName);
        if (AccessMode != KernelMode)
        {
            ProbeForRead(ObjectName,
                         sizeof(UNICODE_STRING),
                         sizeof(USHORT));
            LocalName = *ObjectName;

            ProbeForRead(LocalName.Buffer,
                         LocalName.Length,
                         sizeof(WCHAR));
        }
        else
        {
            /* No probing needed */
            LocalName = *ObjectName;
        }

        /* Make sure there really is a string */
        DPRINT("Probing OK\n");
        if ((StringLength = LocalName.Length))
        {
            /* Check that the size is a valid WCHAR multiple */
            if ((StringLength & (sizeof(WCHAR) - 1)) ||
                /* Check that the NULL-termination below will work */
                (StringLength == (MAXUSHORT - sizeof(WCHAR) + 1)))
            {
                /* PS: Please keep the checks above expanded for clarity */
                DPRINT1("Invalid String Length\n");
                Status = STATUS_OBJECT_NAME_INVALID;
            }
            else
            {
                /* Allocate a non-paged buffer for this string */
                DPRINT("Capturing String\n");
                CapturedName->Length = StringLength;
                CapturedName->MaximumLength = StringLength + sizeof(WCHAR);
                if ((StringBuffer = ExAllocatePoolWithTag(NonPagedPool, 
                                                          StringLength + sizeof(WCHAR),
                                                          OB_NAME_TAG)))
                {                                    
                    /* Copy the string and null-terminate it */
                    RtlMoveMemory(StringBuffer, LocalName.Buffer, StringLength);
                    StringBuffer[StringLength / sizeof(WCHAR)] = UNICODE_NULL;
                    CapturedName->Buffer = StringBuffer;
                    DPRINT("String Captured: %wZ\n", CapturedName);
                }
                else
                {
                    /* Fail */
                    DPRINT1("Out of Memory!\n");
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                }
            }
        }
    }
    _SEH_EXCEPT(_SEH_ExSystemExceptionFilter)
    {
        Status = _SEH_GetExceptionCode();

        /* Remember to free the buffer in case of failure */
        DPRINT1("Failed\n");
        if (StringBuffer) ExFreePool(StringBuffer);
    }
    _SEH_END;
    
    /* Return */
    DPRINT("Returning: %lx\n", Status);
    return Status;
}

NTSTATUS
STDCALL
ObpCaptureObjectAttributes(IN POBJECT_ATTRIBUTES ObjectAttributes,
                           IN KPROCESSOR_MODE AccessMode,
                           IN POBJECT_TYPE ObjectType,
                           IN POBJECT_CREATE_INFORMATION ObjectCreateInfo,
                           OUT PUNICODE_STRING ObjectName)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PSECURITY_DESCRIPTOR SecurityDescriptor;
    PSECURITY_QUALITY_OF_SERVICE SecurityQos;
    PUNICODE_STRING LocalObjectName = NULL;

    /* Zero out the Capture Data */
    DPRINT("ObpCaptureObjectAttributes\n");
    RtlZeroMemory(ObjectCreateInfo, sizeof(OBJECT_CREATE_INFORMATION));
    
    /* SEH everything here for protection */
    _SEH_TRY
    {
        /* Check if we got Oba */
        if (ObjectAttributes)
        {
            if (AccessMode != KernelMode)
            {
                DPRINT("Probing OBA\n");
                ProbeForRead(ObjectAttributes,
                             sizeof(OBJECT_ATTRIBUTES),
                             sizeof(ULONG));
            }
        
            /* Validate the Size and Attributes */
            DPRINT("Validating OBA\n");
            if ((ObjectAttributes->Length != sizeof(OBJECT_ATTRIBUTES)) ||
                (ObjectAttributes->Attributes & ~OBJ_VALID_ATTRIBUTES))
            {
                Status = STATUS_INVALID_PARAMETER;
                DPRINT1("Invalid Size: %lx or Attributes: %lx\n",
                       ObjectAttributes->Length, ObjectAttributes->Attributes); 
                _SEH_LEAVE;
            }
        
            /* Set some Create Info */
            DPRINT("Creating OBCI\n");
            ObjectCreateInfo->RootDirectory = ObjectAttributes->RootDirectory;
            ObjectCreateInfo->Attributes = ObjectAttributes->Attributes;
            LocalObjectName = ObjectAttributes->ObjectName;
            SecurityDescriptor = ObjectAttributes->SecurityDescriptor;
            SecurityQos = ObjectAttributes->SecurityQualityOfService;
        
            /* Validate the SD */
            if (SecurityDescriptor)
            {
                DPRINT("Probing SD: %x\n", SecurityDescriptor);
                Status = SeCaptureSecurityDescriptor(SecurityDescriptor,
                                                     AccessMode,
                                                     NonPagedPool,
                                                     TRUE,
                                                     &ObjectCreateInfo->SecurityDescriptor);
                if(!NT_SUCCESS(Status))
                {
                    DPRINT1("Unable to capture the security descriptor!!!\n");
                    ObjectCreateInfo->SecurityDescriptor = NULL;
                    _SEH_LEAVE;
                }
            
                DPRINT("Probe done\n");
                ObjectCreateInfo->SecurityDescriptorCharge = 2048; /* FIXME */
                ObjectCreateInfo->ProbeMode = AccessMode;
            }
        
            /* Validate the QoS */
            if (SecurityQos)
            {
                if (AccessMode != KernelMode)
                {
                    DPRINT("Probing QoS\n");
                    ProbeForRead(SecurityQos,
                                 sizeof(SECURITY_QUALITY_OF_SERVICE),
                                 sizeof(ULONG));
                }
            
                /* Save Info */
                ObjectCreateInfo->SecurityQualityOfService = *SecurityQos;
                ObjectCreateInfo->SecurityQos = &ObjectCreateInfo->SecurityQualityOfService;
            }
        }
        else
        {
            LocalObjectName = NULL;
        }
    }
    _SEH_EXCEPT(_SEH_ExSystemExceptionFilter)
    {
        Status = _SEH_GetExceptionCode();
        DPRINT1("Failed\n");
    }
    _SEH_END;

    if (NT_SUCCESS(Status))
    {
        /* Now check if the Object Attributes had an Object Name */
        if (LocalObjectName)
        {
            DPRINT("Name Buffer: %wZ\n", LocalObjectName);
            Status = ObpCaptureObjectName(ObjectName,
                                          LocalObjectName,
                                          AccessMode);
        }
        else
        {
            /* Clear the string */
            RtlInitUnicodeString(ObjectName, NULL);

            /* He can't have specified a Root Directory */
            if (ObjectCreateInfo->RootDirectory)
            {
                DPRINT1("Invalid name\n");
                Status = STATUS_OBJECT_NAME_INVALID;
            }
        }
    }
    else
    {
        DPRINT1("Failed to capture, cleaning up\n");
        ObpReleaseCapturedAttributes(ObjectCreateInfo);
    }
    
    DPRINT("Return to caller %x\n", Status);
    return Status;
}

VOID
STDCALL
ObpReleaseCapturedAttributes(IN POBJECT_CREATE_INFORMATION ObjectCreateInfo)
{
    /* Release the SD, it's the only thing we allocated */
    if (ObjectCreateInfo->SecurityDescriptor)
    {
        SeReleaseSecurityDescriptor(ObjectCreateInfo->SecurityDescriptor,
                                    ObjectCreateInfo->ProbeMode,
                                    TRUE);
        ObjectCreateInfo->SecurityDescriptor = NULL;                                        
    }
}

NTSTATUS
NTAPI
ObpAllocateObject(IN POBJECT_CREATE_INFORMATION ObjectCreateInfo,
                  IN PUNICODE_STRING ObjectName,
                  IN POBJECT_TYPE ObjectType,
                  IN ULONG ObjectSize,
                  IN KPROCESSOR_MODE PreviousMode,
                  IN POBJECT_HEADER *ObjectHeader)
{
    POBJECT_HEADER Header;
    BOOLEAN HasHandleInfo = FALSE;
    BOOLEAN HasNameInfo = FALSE;
    BOOLEAN HasCreatorInfo = FALSE;
    POBJECT_HEADER_HANDLE_INFO HandleInfo;
    POBJECT_HEADER_NAME_INFO NameInfo;
    POBJECT_HEADER_CREATOR_INFO CreatorInfo;
    POOL_TYPE PoolType;
    ULONG FinalSize = ObjectSize;
    ULONG Tag;
    PAGED_CODE();
        
    /* If we don't have an Object Type yet, force NonPaged */
    if (!ObjectType) 
    {
        PoolType = NonPagedPool;
        Tag = TAG('O', 'b', 'j', 'T');
    }
    else
    {
        PoolType = ObjectType->TypeInfo.PoolType;
        Tag = ObjectType->Key;
    }
    
    /* Check if the Object has a name */
    if (ObjectName->Buffer) 
    {
        FinalSize += sizeof(OBJECT_HEADER_NAME_INFO);
        HasNameInfo = TRUE;
    }
    
    if (ObjectType)
    {
        /* Check if the Object maintains handle counts */
        if (ObjectType->TypeInfo.MaintainHandleCount)
        {
            FinalSize += sizeof(OBJECT_HEADER_HANDLE_INFO);
            HasHandleInfo = TRUE;
        }
        
        /* Check if the Object maintains type lists */
        if (ObjectType->TypeInfo.MaintainTypeList) 
        {
            FinalSize += sizeof(OBJECT_HEADER_CREATOR_INFO);
            HasCreatorInfo = TRUE;
        }
    }

    /* Allocate memory for the Object and Header */
    Header = ExAllocatePoolWithTag(PoolType, FinalSize, Tag);
    if (!Header) return STATUS_INSUFFICIENT_RESOURCES;
           
    /* Initialize Handle Info */
    if (HasHandleInfo)
    {
        HandleInfo = (POBJECT_HEADER_HANDLE_INFO)Header;
        HandleInfo->SingleEntry.HandleCount = 0;
        Header = (POBJECT_HEADER)(HandleInfo + 1);
    }
       
    /* Initialize the Object Name Info */
    if (HasNameInfo) 
    {
        NameInfo = (POBJECT_HEADER_NAME_INFO)Header;
        NameInfo->Name = *ObjectName;
        NameInfo->Directory = NULL;
        Header = (POBJECT_HEADER)(NameInfo + 1);
    }
    
    /* Initialize Creator Info */
    if (HasCreatorInfo)
    {
        CreatorInfo = (POBJECT_HEADER_CREATOR_INFO)Header;
        CreatorInfo->CreatorUniqueProcess = PsGetCurrentProcess() ?
                                            PsGetCurrentProcessId() : 0;
        InitializeListHead(&CreatorInfo->TypeList);
        Header = (POBJECT_HEADER)(CreatorInfo + 1);
    }
    
    /* Initialize the object header */
    RtlZeroMemory(Header, ObjectSize);
    Header->PointerCount = 1;
    Header->Type = ObjectType;
    Header->Flags = OB_FLAG_CREATE_INFO;
    Header->ObjectCreateInfo = ObjectCreateInfo;
    
    /* Set the Offsets for the Info */
    if (HasHandleInfo)
    {
        Header->HandleInfoOffset = HasNameInfo *
                                   sizeof(OBJECT_HEADER_NAME_INFO) +
                                   sizeof(OBJECT_HEADER_HANDLE_INFO) +
                                   HasCreatorInfo *
                                   sizeof(OBJECT_HEADER_CREATOR_INFO);

        /* Set the flag so we know when freeing */
        Header->Flags |= OB_FLAG_SINGLE_PROCESS;
    }
    if (HasNameInfo)
    {
        Header->NameInfoOffset = sizeof(OBJECT_HEADER_NAME_INFO) + 
                                 HasCreatorInfo *
                                 sizeof(OBJECT_HEADER_CREATOR_INFO);
    }
    if (HasCreatorInfo) Header->Flags |= OB_FLAG_CREATOR_INFO;
    if ((ObjectCreateInfo) && (ObjectCreateInfo->Attributes & OBJ_PERMANENT))
    {
        /* Set the needed flag so we can check */
        Header->Flags |= OB_FLAG_PERMANENT;
    }
    if ((ObjectCreateInfo) && (ObjectCreateInfo->Attributes & OBJ_EXCLUSIVE))
    {
        /* Set the needed flag so we can check */
        Header->Flags |= OB_FLAG_EXCLUSIVE;
    }
    if (PreviousMode == KernelMode)
    {
        /* Set the kernel flag */
        Header->Flags |= OB_FLAG_KERNEL_MODE;
    }
    
    /* Increase the number of objects of this type */
    if (ObjectType) ObjectType->TotalNumberOfObjects++;
    
    /* Return Header */
    *ObjectHeader = Header;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ObpCreateTypeObject(POBJECT_TYPE_INITIALIZER ObjectTypeInitializer,
                    PUNICODE_STRING TypeName,
                    POBJECT_TYPE *ObjectType)
{
    POBJECT_HEADER Header;
    POBJECT_TYPE LocalObjectType;
    ULONG HeaderSize;
    NTSTATUS Status;
    CHAR Tag[4];
    OBP_LOOKUP_CONTEXT Context;

    /* Allocate the Object */
    Status = ObpAllocateObject(NULL, 
                               TypeName,
                               ObTypeObjectType, 
                               sizeof(OBJECT_TYPE) + sizeof(OBJECT_HEADER),
                               KernelMode,
                               (POBJECT_HEADER*)&Header);
    if (!NT_SUCCESS(Status)) return Status;
    LocalObjectType = (POBJECT_TYPE)&Header->Body;
    
    /* Check if this is the first Object Type */
    if (!ObTypeObjectType)
    {
        ObTypeObjectType = LocalObjectType;
        Header->Type = ObTypeObjectType;
        LocalObjectType->TotalNumberOfObjects = 1;
        LocalObjectType->Key = TAG('O', 'b', 'j', 'T');
    }
    else
    {   
        /* Set Tag */
        Tag[0] = TypeName->Buffer[0];
        Tag[1] = TypeName->Buffer[1];
        Tag[2] = TypeName->Buffer[2];
        Tag[3] = TypeName->Buffer[3];
        LocalObjectType->Key = *(PULONG)Tag;
    }
    
    /* Set it up */
    LocalObjectType->TypeInfo = *ObjectTypeInitializer;
    LocalObjectType->Name = *TypeName;
    LocalObjectType->TypeInfo.PoolType = ObjectTypeInitializer->PoolType;

    /* These two flags need to be manually set up */
    Header->Flags |= OB_FLAG_KERNEL_MODE | OB_FLAG_PERMANENT;

    /* Check if we have to maintain a type list */
    if (NtGlobalFlag & FLG_MAINTAIN_OBJECT_TYPELIST)
    {
        /* Enable support */
        LocalObjectType->TypeInfo.MaintainTypeList = TRUE;
    }

    /* Calculate how much space our header'll take up */
    HeaderSize = sizeof(OBJECT_HEADER) + sizeof(OBJECT_HEADER_NAME_INFO) +
                 (ObjectTypeInitializer->MaintainHandleCount ? 
                 sizeof(OBJECT_HEADER_HANDLE_INFO) : 0);

    /* Update the Pool Charges */
    if (ObjectTypeInitializer->PoolType == NonPagedPool)
    {
        LocalObjectType->TypeInfo.DefaultNonPagedPoolCharge += HeaderSize;
    }
    else
    {
        LocalObjectType->TypeInfo.DefaultPagedPoolCharge += HeaderSize;
    }
    
    /* All objects types need a security procedure */
    if (!ObjectTypeInitializer->SecurityProcedure)
    {
        LocalObjectType->TypeInfo.SecurityProcedure = SeDefaultObjectMethod;
    }

    /* Select the Wait Object */
    if (LocalObjectType->TypeInfo.UseDefaultObject)
    {
        /* Add the SYNCHRONIZE access mask since it's waitable */
        LocalObjectType->TypeInfo.ValidAccessMask |= SYNCHRONIZE;

        /* Use the "Default Object", a simple event */
        LocalObjectType->DefaultObject = &ObpDefaultObject;
    }
    /* Special system objects get an optimized hack so they can be waited on */
    else if (TypeName->Length == 8 && !wcscmp(TypeName->Buffer, L"File"))
    {
        LocalObjectType->DefaultObject = (PVOID)FIELD_OFFSET(FILE_OBJECT, Event);
    }
    /* FIXME: When LPC stops sucking, add a hack for Waitable Ports */
    else
    {
        /* No default Object */
        LocalObjectType->DefaultObject = NULL;
    }

    /* Initialize Object Type components */
    ExInitializeResourceLite(&LocalObjectType->Mutex);
    InitializeListHead(&LocalObjectType->TypeList);

    /* Insert it into the Object Directory */
    if (ObpTypeDirectoryObject)
    {
        Context.Directory = ObpTypeDirectoryObject;
        Context.DirectoryLocked = TRUE;
        ObpLookupEntryDirectory(ObpTypeDirectoryObject,
                                TypeName,
                                OBJ_CASE_INSENSITIVE,
                                FALSE,
                                &Context);
        ObpInsertEntryDirectory(ObpTypeDirectoryObject, &Context, Header);
        ObReferenceObject(ObpTypeDirectoryObject);
    }

    *ObjectType = LocalObjectType;
    return Status;
}

/* PUBLIC FUNCTIONS **********************************************************/

NTSTATUS
NTAPI
ObCreateObject(IN KPROCESSOR_MODE ObjectAttributesAccessMode OPTIONAL,
               IN POBJECT_TYPE Type,
               IN POBJECT_ATTRIBUTES ObjectAttributes OPTIONAL,
               IN KPROCESSOR_MODE AccessMode,
               IN OUT PVOID ParseContext OPTIONAL,
               IN ULONG ObjectSize,
               IN ULONG PagedPoolCharge OPTIONAL,
               IN ULONG NonPagedPoolCharge OPTIONAL,
               OUT PVOID *Object)
{
    NTSTATUS Status;
    POBJECT_CREATE_INFORMATION ObjectCreateInfo;
    UNICODE_STRING ObjectName;
    POBJECT_HEADER Header;

    DPRINT("ObCreateObject(Type %p ObjectAttributes %p, Object %p)\n", 
            Type, ObjectAttributes, Object);

    /* Allocate a Buffer for the Object Create Info */
    ObjectCreateInfo = ExAllocatePoolWithTag(NonPagedPool, 
                                             sizeof(*ObjectCreateInfo),
                                             TAG('O','b','C', 'I'));
    if (!ObjectCreateInfo) return STATUS_INSUFFICIENT_RESOURCES;

    /* Capture all the info */
    Status = ObpCaptureObjectAttributes(ObjectAttributes,
                                        ObjectAttributesAccessMode,
                                        Type,
                                        ObjectCreateInfo,
                                        &ObjectName);
    if (NT_SUCCESS(Status))
    {
        /* Validate attributes */
        if (Type->TypeInfo.InvalidAttributes &
            ObjectCreateInfo->Attributes)
        {
            /* Fail */
            Status = STATUS_INVALID_PARAMETER;
        }
        else
        {
            /* Save the pool charges */
            ObjectCreateInfo->PagedPoolCharge = PagedPoolCharge;
            ObjectCreateInfo->NonPagedPoolCharge = NonPagedPoolCharge;

            /* Allocate the Object */
            Status = ObpAllocateObject(ObjectCreateInfo,
                                       &ObjectName,
                                       Type,
                                       ObjectSize + sizeof(OBJECT_HEADER),
                                       AccessMode,
                                       &Header);
            if (NT_SUCCESS(Status))
            {
                /* Return the Object */
                *Object = &Header->Body;
                
                    /* Check if this is a permanent object */
                    if (Header->Flags & OB_FLAG_PERMANENT)
                    {
                        /* Do the privilege check */
                        if (!SeSinglePrivilegeCheck(SeCreatePermanentPrivilege,
                                                    ObjectAttributesAccessMode))
                        {
                            /* Fail */
                            ObpDeallocateObject(*Object);
                            Status = STATUS_PRIVILEGE_NOT_HELD;
                        }
                    }

                    /* Return status */
                return Status;
            }
        }

        /* Release the Capture Info, we don't need it */
        ObpReleaseCapturedAttributes(ObjectCreateInfo);
        if (ObjectName.Buffer) ExFreePool(ObjectName.Buffer);
    }

    /* We failed, so release the Buffer */
    ExFreePool(ObjectCreateInfo);
    return Status;
}

/*++
* @name NtQueryObject
* @implemented NT4
*
*     The NtQueryObject routine <FILLMEIN>
*
* @param ObjectHandle
*        <FILLMEIN>
*
* @param ObjectInformationClass
*        <FILLMEIN>
*
* @param ObjectInformation
*        <FILLMEIN>
*
* @param Length
*        <FILLMEIN>
*
* @param ResultLength
*        <FILLMEIN>
*
* @return STATUS_SUCCESS or appropriate error value.
*
* @remarks None.
*
*--*/
NTSTATUS
NTAPI
NtQueryObject(IN HANDLE ObjectHandle,
              IN OBJECT_INFORMATION_CLASS ObjectInformationClass,
              OUT PVOID ObjectInformation,
              IN ULONG Length,
              OUT PULONG ResultLength OPTIONAL)
{
    OBJECT_HANDLE_INFORMATION HandleInfo;
    POBJECT_HEADER ObjectHeader;
    ULONG InfoLength;
    PVOID Object;
    NTSTATUS Status;
    PAGED_CODE();

    Status = ObReferenceObjectByHandle(ObjectHandle,
                                       0,
                                       NULL,
                                       KeGetPreviousMode(),
                                       &Object,
                                       &HandleInfo);
    if (!NT_SUCCESS (Status)) return Status;

    ObjectHeader = OBJECT_TO_OBJECT_HEADER(Object);

    switch (ObjectInformationClass)
    {
    case ObjectBasicInformation:
        InfoLength = sizeof(OBJECT_BASIC_INFORMATION);
        if (Length != sizeof(OBJECT_BASIC_INFORMATION))
        {
            Status = STATUS_INFO_LENGTH_MISMATCH;
        }
        else
        {
            POBJECT_BASIC_INFORMATION BasicInfo;

            BasicInfo = (POBJECT_BASIC_INFORMATION)ObjectInformation;
            BasicInfo->Attributes = HandleInfo.HandleAttributes;
            BasicInfo->GrantedAccess = HandleInfo.GrantedAccess;
            BasicInfo->HandleCount = ObjectHeader->HandleCount;
            BasicInfo->PointerCount = ObjectHeader->PointerCount;
            BasicInfo->PagedPoolUsage = 0; /* FIXME*/
            BasicInfo->NonPagedPoolUsage = 0; /* FIXME*/
            BasicInfo->NameInformationLength = 0; /* FIXME*/
            BasicInfo->TypeInformationLength = 0; /* FIXME*/
            BasicInfo->SecurityDescriptorLength = 0; /* FIXME*/
            if (ObjectHeader->Type == ObSymbolicLinkType)
            {
                BasicInfo->CreateTime.QuadPart =
                    ((POBJECT_SYMBOLIC_LINK)Object)->CreationTime.QuadPart;
            }
            else
            {
                BasicInfo->CreateTime.QuadPart = (ULONGLONG)0;
            }
            Status = STATUS_SUCCESS;
        }
        break;

    case ObjectNameInformation:
        Status = ObQueryNameString(Object,
                                   (POBJECT_NAME_INFORMATION)ObjectInformation,
                                   Length,
                                   &InfoLength);
        break;

    case ObjectTypeInformation:
        Status = STATUS_NOT_IMPLEMENTED;
        break;

    case ObjectAllTypesInformation:
        Status = STATUS_NOT_IMPLEMENTED;
        break;

    case ObjectHandleInformation:
        InfoLength = sizeof (OBJECT_HANDLE_ATTRIBUTE_INFORMATION);
        if (Length != sizeof (OBJECT_HANDLE_ATTRIBUTE_INFORMATION))
        {
            Status = STATUS_INFO_LENGTH_MISMATCH;
        }
        else
        {
            Status = ObpQueryHandleAttributes(
                ObjectHandle,
                (POBJECT_HANDLE_ATTRIBUTE_INFORMATION)ObjectInformation);
        }
        break;

    default:
        Status = STATUS_INVALID_INFO_CLASS;
        break;
    }

    ObDereferenceObject (Object);

    if (ResultLength != NULL) *ResultLength = InfoLength;

    return Status;
}

/*++
* @name NtSetInformationObject
* @implemented NT4
*
*     The NtSetInformationObject routine <FILLMEIN>
*
* @param ObjectHandle
*        <FILLMEIN>
*
* @param ObjectInformationClass
*        <FILLMEIN>
*
* @param ObjectInformation
*        <FILLMEIN>
*
* @param Length
*        <FILLMEIN>
*
* @return STATUS_SUCCESS or appropriate error value.
*
* @remarks None.
*
*--*/
NTSTATUS
NTAPI
NtSetInformationObject(IN HANDLE ObjectHandle,
                       IN OBJECT_INFORMATION_CLASS ObjectInformationClass,
                       IN PVOID ObjectInformation,
                       IN ULONG Length)
{
    PVOID Object;
    NTSTATUS Status;
    PAGED_CODE();

    if (ObjectInformationClass != ObjectHandleInformation)
        return STATUS_INVALID_INFO_CLASS;

    if (Length != sizeof (OBJECT_HANDLE_ATTRIBUTE_INFORMATION))
        return STATUS_INFO_LENGTH_MISMATCH;

    Status = ObReferenceObjectByHandle(ObjectHandle,
                                       0,
                                       NULL,
                                       KeGetPreviousMode(),
                                       &Object,
                                       NULL);
    if (!NT_SUCCESS (Status)) return Status;

    Status = ObpSetHandleAttributes(ObjectHandle,
                                    (POBJECT_HANDLE_ATTRIBUTE_INFORMATION)
                                    ObjectInformation);

    ObDereferenceObject (Object);
    return Status;
}
/* EOF */
