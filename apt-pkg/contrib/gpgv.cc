// -*- mode: cpp; mode: fold -*-
// Include Files							/*{{{*/
#include <config.h>

#include <apt-pkg/configuration.h>
#include <apt-pkg/error.h>
#include <apt-pkg/fileutl.h>
#include <apt-pkg/gpgv.h>
#include <apt-pkg/strutl.h>

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <apti18n.h>
									/*}}}*/
static char * GenerateTemporaryFileTemplate(const char *basename)	/*{{{*/
{
   std::string out;
   std::string tmpdir = GetTempDir();
   strprintf(out,  "%s/%s.XXXXXX", tmpdir.c_str(), basename);
   return strdup(out.c_str());
}
									/*}}}*/
// ExecGPGV - returns the command needed for verify			/*{{{*/
// ---------------------------------------------------------------------
/* Generating the commandline for calling gpg is somehow complicated as
   we need to add multiple keyrings and user supplied options.
   Also, as gpg has no options to enforce a certain reduced style of
   clear-signed files (=the complete content of the file is signed and
   the content isn't encoded) we do a divide and conquer approach here
   and split up the clear-signed file in message and signature for gpg.
   And as a cherry on the cake, we use our apt-key wrapper to do part
   of the lifting in regards to merging keyrings. Fun for the whole family.
*/
static bool iovprintf(std::ostream &out, const char *format,
		      va_list &args, ssize_t &size) {
   char *S = (char*)malloc(size);
   ssize_t const n = vsnprintf(S, size, format, args);
   if (n > -1 && n < size) {
      out << S;
      free(S);
      return true;
   } else {
      if (n > -1)
	 size = n + 1;
      else
	 size *= 2;
   }
   free(S);
   return false;
}
static void APT_PRINTF(4) apt_error(std::ostream &outterm, int const statusfd, int fd[2], const char *format, ...)
{
   std::ostringstream outstr;
   std::ostream &out = (statusfd == -1) ? outterm : outstr;
   va_list args;
   ssize_t size = 400;
   while (true) {
      bool ret;
      va_start(args,format);
      ret = iovprintf(out, format, args, size);
      va_end(args);
      if (ret == true)
	 break;
   }
   if (statusfd != -1)
   {
      auto const errtag = "[APTKEY:] ERROR ";
      outstr << '\n';
      auto const errtext = outstr.str();
      if (FileFd::Write(fd[1], errtag, strlen(errtag)) == false ||
	    FileFd::Write(fd[1], errtext.data(), errtext.size()) == false)
	 outterm << errtext << std::flush;
   }
}
void ExecGPGV(std::string const &File, std::string const &FileGPG,
             int const &statusfd, int fd[2], std::string const &key)
{
   #define EINTERNAL 111
   std::string const aptkey = _config->Find("Dir::Bin::apt-key", CMAKE_INSTALL_FULL_BINDIR "/apt-key");

   bool const Debug = _config->FindB("Debug::Acquire::gpgv", false);
   struct exiter {
      std::vector<const char *> files;
      void operator ()(int code) APT_NORETURN {
	 std::for_each(files.begin(), files.end(), unlink);
	 exit(code);
      }
   } local_exit;


   std::vector<const char *> Args;
   Args.reserve(10);

   Args.push_back(aptkey.c_str());
   Args.push_back("--quiet");
   Args.push_back("--readonly");
   auto const keysFileFpr = VectorizeString(key, ',');
   for (auto const &k: keysFileFpr)
   {
      if (unlikely(k.empty()))
	 continue;
      if (k[0] == '/')
      {
	 Args.push_back("--keyring");
	 Args.push_back(k.c_str());
      }
      else
      {
	 Args.push_back("--keyid");
	 Args.push_back(k.c_str());
      }
   }
   Args.push_back("verify");

   char statusfdstr[10];
   if (statusfd != -1)
   {
      Args.push_back("--status-fd");
      snprintf(statusfdstr, sizeof(statusfdstr), "%i", statusfd);
      Args.push_back(statusfdstr);
   }

   Configuration::Item const *Opts;
   Opts = _config->Tree("Acquire::gpgv::Options");
   if (Opts != 0)
   {
      Opts = Opts->Child;
      for (; Opts != 0; Opts = Opts->Next)
      {
	 if (Opts->Value.empty() == true)
	    continue;
	 Args.push_back(Opts->Value.c_str());
      }
   }

   enum  { DETACHED, CLEARSIGNED } releaseSignature = (FileGPG != File) ? DETACHED : CLEARSIGNED;
   char * sig = NULL;
   char * data = NULL;
   char * conf = nullptr;

   // Dump the configuration so apt-key picks up the correct Dir values
   {
      conf = GenerateTemporaryFileTemplate("apt.conf");
      if (conf == nullptr) {
	 apt_error(std::cerr, statusfd, fd, "Couldn't create tempfile names for passing config to apt-key");
	 local_exit(EINTERNAL);
      }
      int confFd = mkstemp(conf);
      if (confFd == -1) {
	 apt_error(std::cerr, statusfd, fd, "Couldn't create temporary file %s for passing config to apt-key", conf);
	 local_exit(EINTERNAL);
      }
      local_exit.files.push_back(conf);

      std::ofstream confStream(conf);
      close(confFd);
      _config->Dump(confStream);
      confStream.close();
      setenv("APT_CONFIG", conf, 1);
   }

   if (releaseSignature == DETACHED)
   {
      Args.push_back(FileGPG.c_str());
      Args.push_back(File.c_str());
   }
   else // clear-signed file
   {
      sig = GenerateTemporaryFileTemplate("apt.sig");
      data = GenerateTemporaryFileTemplate("apt.data");
      if (sig == NULL || data == NULL)
      {
	 apt_error(std::cerr, statusfd, fd, "Couldn't create tempfile names for splitting up %s", File.c_str());
	 local_exit(EINTERNAL);
      }

      int const sigFd = mkstemp(sig);
      int const dataFd = mkstemp(data);
      if (dataFd != -1)
	 local_exit.files.push_back(data);
      if (sigFd != -1)
	 local_exit.files.push_back(sig);
      if (sigFd == -1 || dataFd == -1)
      {
	 apt_error(std::cerr, statusfd, fd, "Couldn't create tempfiles for splitting up %s", File.c_str());
	 local_exit(EINTERNAL);
      }

      FileFd signature;
      signature.OpenDescriptor(sigFd, FileFd::WriteOnly, true);
      FileFd message;
      message.OpenDescriptor(dataFd, FileFd::WriteOnly, true);

      if (signature.Failed() == true || message.Failed() == true ||
	    SplitClearSignedFile(File, &message, nullptr, &signature) == false)
      {
	 apt_error(std::cerr, statusfd, fd, "Splitting up %s into data and signature failed", File.c_str());
	 local_exit(112);
      }
      Args.push_back(sig);
      Args.push_back(data);
   }

   Args.push_back(NULL);

   if (Debug == true)
   {
      std::clog << "Preparing to exec: ";
      for (std::vector<const char *>::const_iterator a = Args.begin(); *a != NULL; ++a)
	 std::clog << " " << *a;
      std::clog << std::endl;
   }

   if (statusfd != -1)
   {
      int const nullfd = open("/dev/null", O_WRONLY);
      close(fd[0]);
      // Redirect output to /dev/null; we read from the status fd
      if (statusfd != STDOUT_FILENO)
	 dup2(nullfd, STDOUT_FILENO);
      if (statusfd != STDERR_FILENO)
	 dup2(nullfd, STDERR_FILENO);
      // Redirect the pipe to the status fd (3)
      dup2(fd[1], statusfd);

      putenv((char *)"LANG=");
      putenv((char *)"LC_ALL=");
      putenv((char *)"LC_MESSAGES=");
   }


   // We have created tempfiles we have to clean up
   // and we do an additional check, so fork yet another time …
   pid_t pid = ExecFork();
   if(pid < 0) {
      apt_error(std::cerr, statusfd, fd, "Fork failed for %s to check %s", Args[0], File.c_str());
      local_exit(EINTERNAL);
   }
   if(pid == 0)
   {
      if (statusfd != -1)
	 dup2(fd[1], statusfd);
      execvp(Args[0], (char **) &Args[0]);
      apt_error(std::cerr, statusfd, fd, "Couldn't execute %s to check %s", Args[0], File.c_str());
      local_exit(EINTERNAL);
   }

   // Wait and collect the error code - taken from WaitPid as we need the exact Status
   int Status;
   while (waitpid(pid,&Status,0) != pid)
   {
      if (errno == EINTR)
	 continue;
      apt_error(std::cerr, statusfd, fd, _("Waited for %s but it wasn't there"), "apt-key");
      local_exit(EINTERNAL);
   }

   // check if it exit'ed normally …
   if (WIFEXITED(Status) == false)
   {
      apt_error(std::cerr, statusfd, fd, _("Sub-process %s exited unexpectedly"), "apt-key");
      local_exit(EINTERNAL);
   }

   // … and with a good exit code
   if (WEXITSTATUS(Status) != 0)
   {
      // we forward the statuscode, so don't generate a message on the fd in this case
      apt_error(std::cerr, -1, fd, _("Sub-process %s returned an error code (%u)"), "apt-key", WEXITSTATUS(Status));
      local_exit(WEXITSTATUS(Status));
   }

   // everything fine
   local_exit(0);
}
									/*}}}*/
// SplitClearSignedFile - split message into data/signature		/*{{{*/
static bool GetLineErrno(std::unique_ptr<char, decltype(&free)> &buffer, size_t *n, FILE *stream, std::string const &InFile, bool acceptEoF = false)
{
   errno = 0;
   auto lineptr = buffer.release();
   auto const result = getline(&lineptr, n, stream);
   buffer.reset(lineptr);
   if (errno != 0)
      return _error->Errno("getline", "Could not read from %s", InFile.c_str());
   if (result == -1)
   {
      if (acceptEoF)
	 return false;
      return _error->Error("Splitting of clearsigned file %s failed as it doesn't contain all expected parts", InFile.c_str());
   }
   // We remove all whitespaces including newline here as
   // a) gpgv ignores them for signature
   // b) we can write out a \n in code later instead of dealing with \r\n or not
   _strrstrip(buffer.get());
   return true;
}
bool SplitClearSignedFile(std::string const &InFile, FileFd * const ContentFile,
      std::vector<std::string> * const ContentHeader, FileFd * const SignatureFile)
{
   std::unique_ptr<FILE, decltype(&fclose)> in{fopen(InFile.c_str(), "r"), &fclose};
   if (in.get() == nullptr)
      return _error->Errno("fopen", "can not open %s", InFile.c_str());

   struct ScopedErrors
   {
      ScopedErrors() { _error->PushToStack(); }
      ~ScopedErrors() { _error->MergeWithStack(); }
   } scoped;
   std::unique_ptr<char, decltype(&free)> buf{nullptr, &free};
   size_t buf_size = 0;

   // start of the message
   if (GetLineErrno(buf, &buf_size, in.get(), InFile) == false)
      return false; // empty or read error
   if (strcmp(buf.get(), "-----BEGIN PGP SIGNED MESSAGE-----") != 0)
   {
      // this might be an unsigned file we don't want to report errors for,
      // but still finish unsuccessful none the less.
      while (GetLineErrno(buf, &buf_size, in.get(), InFile, true))
	 if (strcmp(buf.get(), "-----BEGIN PGP SIGNED MESSAGE-----") == 0)
	    return _error->Error("Clearsigned file '%s' does not start with a signed message block.", InFile.c_str());

      return false;
   }

   // save "Hash" Armor Headers
   while (true)
   {
      if (GetLineErrno(buf, &buf_size, in.get(), InFile) == false)
	 return false;
      if (*buf == '\0')
	 break; // empty line ends the Armor Headers
      if (ContentHeader != NULL && strncmp(buf.get(), "Hash: ", strlen("Hash: ")) == 0)
	 ContentHeader->push_back(buf.get());
   }

   // the message itself
   bool first_line = true;
   bool good_write = true;
   while (true)
   {
      if (good_write == false || GetLineErrno(buf, &buf_size, in.get(), InFile) == false)
	 return false;

      if (strcmp(buf.get(), "-----BEGIN PGP SIGNATURE-----") == 0)
      {
	 if (SignatureFile != nullptr)
	 {
	    good_write &= SignatureFile->Write(buf.get(), strlen(buf.get()));
	    good_write &= SignatureFile->Write("\n", 1);
	 }
	 break;
      }

      // we don't have any fields which need dash-escaped,
      // but implementations are free to encode all lines …
      char const *dashfree = buf.get();
      if (strncmp(dashfree, "- ", 2) == 0)
	 dashfree += 2;
      if (first_line == true) // first line does not need a newline
	 first_line = false;
      else if (ContentFile != nullptr)
	 good_write &= ContentFile->Write("\n", 1);
      if (ContentFile != nullptr)
	 good_write &= ContentFile->Write(dashfree, strlen(dashfree));
   }

   // collect all signatures
   bool open_signature = true;
   while (true)
   {
      if (good_write == false)
	 return false;
      if (GetLineErrno(buf, &buf_size, in.get(), InFile, true) == false)
	 break;

      if (open_signature && strcmp(buf.get(), "-----END PGP SIGNATURE-----") == 0)
	 open_signature = false;
      else if (open_signature == false && strcmp(buf.get(), "-----BEGIN PGP SIGNATURE-----") == 0)
	 open_signature = true;
      else if (open_signature == false)
	 return _error->Error("Clearsigned file '%s' contains unsigned lines.", InFile.c_str());

      if (SignatureFile != nullptr)
      {
	 good_write &= SignatureFile->Write(buf.get(), strlen(buf.get()));
	 good_write &= SignatureFile->Write("\n", 1);
      }
   }
   if (open_signature == true)
      return _error->Error("Signature in file %s wasn't closed", InFile.c_str());

   // Flush the files
   if (SignatureFile != nullptr)
      SignatureFile->Flush();
   if (ContentFile != nullptr)
      ContentFile->Flush();

   // Catch-all for "unhandled" read/sync errors
   if (_error->PendingError())
      return false;
   return true;
}
									/*}}}*/
bool OpenMaybeClearSignedFile(std::string const &ClearSignedFileName, FileFd &MessageFile) /*{{{*/
{
   char * const message = GenerateTemporaryFileTemplate("fileutl.message");
   int const messageFd = mkstemp(message);
   if (messageFd == -1)
   {
      free(message);
      return _error->Errno("mkstemp", "Couldn't create temporary file to work with %s", ClearSignedFileName.c_str());
   }
   // we have the fd, that's enough for us
   unlink(message);
   free(message);

   MessageFile.OpenDescriptor(messageFd, FileFd::ReadWrite | FileFd::BufferedWrite, true);
   if (MessageFile.Failed() == true)
      return _error->Error("Couldn't open temporary file to work with %s", ClearSignedFileName.c_str());

   _error->PushToStack();
   bool const splitDone = SplitClearSignedFile(ClearSignedFileName, &MessageFile, NULL, NULL);
   bool const errorDone = _error->PendingError();
   _error->MergeWithStack();
   if (splitDone == false)
   {
      MessageFile.Close();

      if (errorDone == true)
	 return false;

      // we deal with an unsigned file
      MessageFile.Open(ClearSignedFileName, FileFd::ReadOnly);
   }
   else // clear-signed
   {
      if (MessageFile.Seek(0) == false)
	 return _error->Errno("lseek", "Unable to seek back in message for file %s", ClearSignedFileName.c_str());
   }

   return MessageFile.Failed() == false;
}
									/*}}}*/
