// -*- mode: cpp; mode: fold -*-
// Description								/*{{{*/
// $Id: acquire-item.h,v 1.17 1999/03/27 03:02:38 jgg Exp $
/* ######################################################################

   Acquire Item - Item to acquire

   When an item is instantiated it will add it self to the local list in
   the Owner Acquire class. Derived classes will then call QueueURI to 
   register all the URI's they wish to fetch at the initial moment.   
   
   Two item classes are provided to provide functionality for downloading
   of Index files and downloading of Packages.
   
   A Archive class is provided for downloading .deb files. It does Md5
   checking and source location as well as a retry algorithm.
   
   ##################################################################### */
									/*}}}*/
#ifndef PKGLIB_ACQUIRE_ITEM_H
#define PKGLIB_ACQUIRE_ITEM_H

#include <apt-pkg/acquire.h>
#include <apt-pkg/sourcelist.h>
#include <apt-pkg/pkgrecords.h>

#ifdef __GNUG__
#pragma interface "apt-pkg/acquire-item.h"
#endif 

// Item to acquire
class pkgAcquire::Item
{  
   protected:
   
   // Some private helper methods for registering URIs
   pkgAcquire *Owner;
   inline void QueueURI(ItemDesc &Item)
                 {Owner->Enqueue(Item);};
   inline void Dequeue() {Owner->Dequeue(this);};
   
   // Safe rename function with timestamp preservation
   void Rename(string From,string To);
   
   public:

   // State of the item
   enum {StatIdle, StatFetching, StatDone, StatError} Status;
   string ErrorText;
   unsigned long FileSize;
   unsigned long PartialSize;   
   char *Mode;
   unsigned long ID;
   bool Complete;
   bool Local;

   // Number of queues we are inserted into
   unsigned int QueueCounter;
   
   // File to write the fetch into
   string DestFile;

   // Action members invoked by the worker
   virtual void Failed(string Message,pkgAcquire::MethodConfig *Cnf);
   virtual void Done(string Message,unsigned long Size,string Md5Hash);
   virtual void Start(string Message,unsigned long Size);
   virtual string Custom600Headers() {return string();};
   
   // Inquire functions
   virtual string MD5Sum() {return string();};
   virtual string Describe() = 0;
         
   Item(pkgAcquire *Owner);
   virtual ~Item();
};

// Item class for index files
class pkgAcqIndex : public pkgAcquire::Item
{
   protected:
   
   const pkgSourceList::Item *Location;
   bool Decompression;
   bool Erase;
   pkgAcquire::ItemDesc Desc;
   
   public:
   
   // Specialized action members
   virtual void Done(string Message,unsigned long Size,string Md5Hash);   
   virtual string Custom600Headers();
   virtual string Describe();

   pkgAcqIndex(pkgAcquire *Owner,const pkgSourceList::Item *Location);
};

// Item class for index files
class pkgAcqIndexRel : public pkgAcquire::Item
{
   protected:
   
   const pkgSourceList::Item *Location;
   pkgAcquire::ItemDesc Desc;
   
   public:
   
   // Specialized action members
   virtual void Failed(string Message,pkgAcquire::MethodConfig *Cnf);
   virtual void Done(string Message,unsigned long Size,string Md5Hash);   
   virtual string Custom600Headers();
   virtual string Describe();
   
   pkgAcqIndexRel(pkgAcquire *Owner,const pkgSourceList::Item *Location);
};

// Item class for archive files
class pkgAcqArchive : public pkgAcquire::Item
{
   protected:
   
   // State information for the retry mechanism
   pkgCache::VerIterator Version;
   pkgAcquire::ItemDesc Desc;
   pkgSourceList *Sources;
   pkgRecords *Recs;
   string MD5;
   string &StoreFilename;
   pkgCache::VerFileIterator Vf;
   unsigned int Retries;

   // Queue the next available file for download.
   bool QueueNext();
   
   public:
   
   // Specialized action members
   virtual void Failed(string Message,pkgAcquire::MethodConfig *Cnf);
   virtual void Done(string Message,unsigned long Size,string Md5Hash);
   virtual string Describe();
   virtual string MD5Sum() {return MD5;};
   
   pkgAcqArchive(pkgAcquire *Owner,pkgSourceList *Sources,
		 pkgRecords *Recs,pkgCache::VerIterator const &Version,
		 string &StoreFilename);
};

#endif
