// -*- mode: cpp; mode: fold -*-
// Description								/*{{{*/
// $Id: orderlist.h,v 1.5 1999/07/12 03:40:38 jgg Exp $
/* ######################################################################

   Order List - Represents and Manipulates an ordered list of packages.
   
   A list of packages can be ordered by a number of conflicting criteria
   each given a specific priority. Each package also has a set of flags
   indicating some usefull things about it that are derived in the 
   course of sorting. The pkgPackageManager class uses this class for
   all of it's installation ordering needs.
   
   ##################################################################### */
									/*}}}*/
// Header section: pkglib
#ifndef PKGLIB_ORDERLIST_H
#define PKGLIB_ORDERLIST_H

#ifdef __GNUG__
#pragma interface "apt-pkg/orderlist.h"
#endif 

#include <apt-pkg/pkgcache.h>

class pkgDepCache;
class pkgOrderList
{
   protected:

   pkgDepCache &Cache;
   
   // Bring some usefull types into the local scope
   typedef pkgCache::PkgIterator PkgIterator;
   typedef pkgCache::VerIterator VerIterator;
   typedef pkgCache::DepIterator DepIterator;
   typedef pkgCache::PrvIterator PrvIterator;
   typedef pkgCache::Package Package;
   typedef pkgCache::Version Version;
   typedef bool (pkgOrderList::*DepFunc)(DepIterator D);

   // These are the currently selected ordering functions
   DepFunc Primary;
   DepFunc Secondary;
   DepFunc RevDepends;
   DepFunc Remove;

   // State
   Package **End;
   Package **List;
   string *FileList;
   DepIterator Loops[20];
   int LoopCount;
   int Depth;
   unsigned char *Flags;
   
   // Main visit function
   bool VisitNode(PkgIterator Pkg);
   bool VisitDeps(DepFunc F,PkgIterator Pkg);
   bool VisitRDeps(DepFunc F,PkgIterator Pkg);
   bool VisitRProvides(DepFunc F,VerIterator Ver);
   bool VisitProvides(DepIterator Pkg,bool Critical);
   
   // Dependency checking functions.
   bool DepUnPackCrit(DepIterator D);
   bool DepUnPackPreD(DepIterator D);
   bool DepUnPackPre(DepIterator D);
   bool DepUnPackDep(DepIterator D);
   bool DepConfigure(DepIterator D);
   bool DepRemove(DepIterator D);
   
   // Analysis helpers
   bool AddLoop(DepIterator D);
   bool CheckDep(DepIterator D);
   bool DoRun();
   
   // For pre sorting
   static pkgOrderList *Me;
   static int OrderCompareA(const void *a, const void *b);
   static int OrderCompareB(const void *a, const void *b);
   int FileCmp(PkgIterator A,PkgIterator B);
   
   public:

   typedef Package **iterator;
   
   // State flags
   enum Flags {Added = (1 << 0), AddPending = (1 << 1),
               Immediate = (1 << 2), Loop = (1 << 3),
               UnPacked = (1 << 4), Configured = (1 << 5),
               Removed = (1 << 6),
               InList = (1 << 7),
               States = (UnPacked | Configured | Removed)};

   // Flag manipulators
   inline bool IsFlag(PkgIterator Pkg,unsigned long F) {return (Flags[Pkg->ID] & F) == F;};
   inline bool IsFlag(Package *Pkg,unsigned long F) {return (Flags[Pkg->ID] & F) == F;};
   void Flag(PkgIterator Pkg,unsigned long State, unsigned long F) {Flags[Pkg->ID] = (Flags[Pkg->ID] & (~F)) | State;};
   inline void Flag(PkgIterator Pkg,unsigned long F) {Flags[Pkg->ID] |= F;};
   inline void Flag(Package *Pkg,unsigned long F) {Flags[Pkg->ID] |= F;};
   inline bool IsNow(PkgIterator Pkg) {return (Flags[Pkg->ID] & States) == 0;};
   bool IsMissing(PkgIterator Pkg);
   void WipeFlags(unsigned long F);
   void SetFileList(string *FileList) {this->FileList = FileList;};
   
   // Accessors
   inline iterator begin() {return List;};
   inline iterator end() {return End;};
   inline void push_back(Package *Pkg) {*(End++) = Pkg;};
   inline void push_back(PkgIterator Pkg) {*(End++) = Pkg;};
   inline void pop_back() {End--;};
   inline bool empty() {return End == List;};
   inline unsigned int size() {return End - List;};
   
   // Ordering modes
   bool OrderCritical();
   bool OrderUnpack(string *FileList = 0);
   bool OrderConfigure();

   int Score(PkgIterator Pkg);

   pkgOrderList(pkgDepCache &Cache);
   ~pkgOrderList();
};

#endif
