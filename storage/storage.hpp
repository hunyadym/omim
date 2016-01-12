#pragma once

#include "storage/country.hpp"
#include "storage/index.hpp"
#include "storage/map_files_downloader.hpp"
#include "storage/queued_country.hpp"
#include "storage/storage_defines.hpp"

#include "platform/local_country_file.hpp"

#include "std/function.hpp"
#include "std/list.hpp"
#include "std/set.hpp"
#include "std/shared_ptr.hpp"
#include "std/string.hpp"
#include "std/unique_ptr.hpp"
#include "std/vector.hpp"

namespace storage
{
/// This class is used for downloading, updating and deleting maps.
class Storage
{
public:
  struct StatusCallback;
  using TUpdate = function<void(platform::LocalCountryFile const &)>;

private:
  /// We support only one simultaneous request at the moment
  unique_ptr<MapFilesDownloader> m_downloader;

  /// stores timestamp for update checks
  int64_t m_currentVersion;

  TCountriesContainer m_countries;

  using TQueue = list<QueuedCountry>;

  /// @todo. It appeared that our application uses m_queue from
  /// different threads without any synchronization. To reproduce it
  /// just download a map "from the map" on Android. (CountryStatus is
  /// called from a different thread.)  It's necessary to check if we
  /// can call all the methods from a single thread using
  /// RunOnUIThread.  If not, at least use a syncronization object.
  TQueue m_queue;

  /// stores countries whose download has failed recently
  using TCountriesSet = set<TCountryId>;
  TCountriesSet m_failedCountries;

  using TLocalFilePtr = shared_ptr<platform::LocalCountryFile>;
  map<TCountryId, list<TLocalFilePtr>> m_localFiles;

  // Our World.mwm and WorldCoasts.mwm are fake countries, together with any custom mwm in data
  // folder.
  map<platform::CountryFile, TLocalFilePtr> m_localFilesForFakeCountries;

  /// used to correctly calculate total country download progress with more than 1 file
  /// <current, total>
  MapFilesDownloader::TProgress m_countryProgress;

  /// @name Communicate with GUI
  //@{
  typedef function<void(TCountryId const &)> TChangeCountryFunction;
  typedef function<void(TCountryId const &, LocalAndRemoteSizeT const &)> TProgressFunction;

  int m_currentSlotId;

  list<StatusCallback> m_statusCallbacks;

  struct CountryObservers
  {
    TChangeCountryFunction m_changeCountryFn;
    TProgressFunction m_progressFn;
    int m_slotId;
  };

  typedef list<CountryObservers> ObserversContT;
  ObserversContT m_observers;
  //@}

  // This function is called each time all files requested for a
  // country were successfully downloaded.
  TUpdate m_update;

  // If |m_dataDir| is not empty Storage will create version directories and download maps in
  // platform::WritableDir/|m_dataDir|/. Not empty |m_dataDir| can be used only for
  // downloading maps to a special place but not for continue working with them from this place.
  string m_dataDir;

  void DownloadNextCountryFromQueue();

  void LoadCountriesFile(string const & pathToCountriesFile,
                         string const & dataDir, TMapping * mapping = nullptr);

  void ReportProgress(TCountryId const & countryId, pair<int64_t, int64_t> const & p);

  /// Called on the main thread by MapFilesDownloader when list of
  /// suitable servers is received.
  void OnServerListDownloaded(vector<string> const & urls);

  /// Called on the main thread by MapFilesDownloader when
  /// downloading of a map file succeeds/fails.
  void OnMapFileDownloadFinished(bool success, MapFilesDownloader::TProgress const & progress);

  /// Periodically called on the main thread by MapFilesDownloader
  /// during the downloading process.
  void OnMapFileDownloadProgress(MapFilesDownloader::TProgress const & progress);

  bool RegisterDownloadedFiles(TCountryId const & countryId, MapOptions files);
  void OnMapDownloadFinished(TCountryId const & countryId, bool success, MapOptions files);

  /// Initiates downloading of the next file from the queue.
  void DownloadNextFile(QueuedCountry const & country);

public:
  /// \brief Storage will create its directories in Writable Directory
  /// (gotten with platform::WritableDir) by default.
  /// \param pathToCountriesFile is a name of countries.txt file.
  /// \param dataDir If |dataDir| is not empty Storage will create its directory in WritableDir/|dataDir|.
  /// \note if |dataDir| is not empty the instance of Storage can be used only for downloading map files
  /// but not for continue working with them.
  /// If |dataDir| is not empty the work flow is
  /// * create a instance of Storage with a special countries.txt and |dataDir|
  /// * download some maps to WritableDir/|dataDir|
  /// * destroy the instance of Storage and move the downloaded maps to proper place
  Storage(string const & pathToCountriesFile = COUNTRIES_FILE, string const & dataDir = string());
  /// \brief This constructor should be used for testing only.
  Storage(string const & referenceCountriesTxtJsonForTesting,
          unique_ptr<MapFilesDownloader> mapDownloaderForTesting);

  /// @name Interface with clients (Android/iOS).
  /// \brief It represents the interface which can be used by clients (Android/iOS).
  /// The term node means an mwm or a group of mwm like a big country.
  /// The term node id means a string id of mwm or a group of mwm. The sting contains
  /// a name of file with mwm of a name country(territory).
  //@{
  enum class ErrorCode;
  using TOnSearchResultCallback = function<void (TCountryId const &)>;
  using TOnStatusChangedCallback = function<void (TCountryId const &)>;
  using TOnErrorCallback = function<void (TCountryId const &, ErrorCode)>;

  /// \brief This enum describes status of a downloaded mwm or a group of downloaded mwm.
  enum class ClientNodeStatus
  {
    UpToDate,              /**< Downloaded mwm(s) is up to date. No need update it. */
    DownloadingInProcess,  /**< Downloading a new mwm or updating a old one. */
    DownloadWasPaused,     /**< Downloading was paused or stopped by some reasons. E.g lost connection. */
    NeedsToUpdate,         /**< An update for a downloaded mwm is ready according to county_attributes.txt. */
    InQueue,               /**< A mwm is waiting for downloading in the queue. */
  };

  /// \brief Contains all properties for a node on the server.
  struct ServerNodeAttrs
  {
    /// If it's not an extandable m_nodeSize is size of one mwm.
    /// Otherwise m_nodeSize is a sum of all mwm sizes which belong to the group.
    size_t m_nodeSize;
    /// If the node is expandalbe (a big country) m_childrenCounter is number of children of this node.
    /// If the node isn't expandable m_childrenCounter == -1.
    int m_childrenCounter;
    /// parentId is a node id of parent of the node.
    /// If the node is "world" (that means the root) parentId == "".
    TCountryId parentId;
  };

  /// \brief Contains all properties for a downloaded mwm.
  /// It's applicable for expandable and not expandable node id.
  struct ClientNodeAttrs
  {
    /// If it's not an extandable node m_nodeSize is size of one mwm.
    /// Otherwise m_nodeSize is a sum of all mwm sizes which belong to the group and
    /// have been downloaded.
    size_t m_nodeSize;
    /// If the node is expandable (a big country) m_mapsDownloaded is number of maps has been downloaded.
    /// If the node isn't expandable m_mapsDownloaded == -1.
    int m_mapsDownloaded;
    /// \brief It's an mwm version which was taken for mwm header.
    /// @TODO Discuss a version format. It should represent date and time (one second precision).
    /// It should be converted easily to unix time.
    /// \note It's set to zero in it's attributes of expandable node.
    size_t m_mwmVersion;
    /// A number for 0 to 100. It reflects downloading progress in case of
    /// downloading and updating mwm.
    uint8_t m_downloadingProgress;
    ClientNodeStatus m_status;
  };

  /// \brief Error code of MapRepository.
  enum class ErrorCode
  {
    NoError,                /**< An operation was finished without errors. */
    NotEnoughSpace,         /**< No space on flash memory to download a file. */
    NoInternetConnection,   /**< No internet connection. */
  };

  /// \brief Information for "Update all mwms" button.
  struct UpdateInfo
  {
    size_t m_numberOfMwmFilesToUpdate;
    size_t m_totalUpdateSizeInBytes;
  };

  struct StatusCallback
  {
    /// \brief m_onStatusChanged is called by MapRepository when status of
    /// a node is changed. If this method is called for an mwm it'll be called for
    /// every its parent and grandparents.
    /// \param CountryId is id of mwm or an mwm group which status has been changed.
    TOnStatusChangedCallback m_onStatusChanged;
    /// \brief m_onError is called when an error happend while async operation.
    /// \note A client should be ready for any value of error.
    TOnErrorCallback m_onError;
  };

  void SaveDownloadQueue();
  void RestoreDownloadQueue();

  /// \brief Returns root country id of the county tree.
  TCountryId const GetRootId() const;
  /// \brief Returns children node ids by a parent. For example GetChildren(GetRootId())
  /// returns all counties ids. It's content of map downloader list by default.
  vector<TCountryId> GetChildren(TCountryId const & parent) const;
  /// \brief Fills localChildren with children of parent.
  /// The result of the method is composed in a special way because of design requirements.
  /// If a direct child (of parent) contains two or more downloaded mwms the direct child id will be added to result.
  /// If a direct child (of parent) contains one downloaded mwm the mwm id will be added to result.
  /// If there's no downloaded mwms contained by a direct child the direct child id will not be added to result.
  /// \param parent is a parent acoording to countries.txt.
  /// \note. This method puts to localChildren only real maps which have been written in coutries.txt. It means
  /// the method does not put to localChildren neither custom maps generated by user
  /// nor World.mwm and WorldCoasts.mwm.
  void GetDownloadedChildren(TCountryId const & parent, vector<TCountryId> & localChildren) const;

  /// \brief Returns current version for mwms which are available on the server.
  inline int64_t GetCurrentDataVersion() const { return m_currentVersion; }
  /// \brief Returns true if the node with countryId has been downloaded and false othewise.
  /// If countryId is a expandable returns true if all mwms which belongs to it have downloaded.
  /// Returns false if countryId is an unknown string.
  /// \note The method return false for custom maps generated by user
  /// and World.mwm and WorldCoasts.mwm.
  bool IsNodeDownloaded(TCountryId const & countryId) const;

  /// \brief Gets attributes for an available on server node by countryId.
  /// \param countryId is id of single mwm or a group of mwm.
  /// \param ServerNodeAttrs is filled with attibutes of node which is available for downloading.
  /// I.e. is written in county_attributes.txt.
  /// \return false in case of error and true otherwise.
  bool GetServerNodeAttrs(TCountryId const & countryId, ClientNodeAttrs & serverNodeAttrs) const;
  /// \brief Gets attributes for downloaded a node by countryId.
  /// \param ClientNodeAttrs is filled with attibutes in this method.
  /// \return false in case of error and true otherwise.
  bool GetClientNodeAttrs(TCountryId const & countryId, ClientNodeAttrs & clientNodeAttrs) const;

  /// \brief Downloads one node (expandable or not) by countryId.
  /// If node is expandable downloads all children (grandchildren) by the node
  /// until they havn't been downloaded before. Update all downloaded mwm if it's necessary.
  /// \return false in case of error and true otherwise.
  bool DownloadNode(TCountryId const & countryId);
  /// \brief Delete one node (expandable or not).
  /// \return false in case of error and true otherwise.
  bool DeleteNode(TCountryId const & countryId);
  /// \brief Updates one node (expandable or not).
  /// \note If you want to update all the maps and this update is without changing
  /// borders or hierarchy just call UpdateNode(GetRootId()).
  /// \return false in case of error and true otherwise.
  bool UpdateNode(TCountryId const & countryId);
  /// \brief Cancels downloading a node if the downloading is in process.
  /// \return false in case of error and true otherwise.
  bool CancelNodeDownloading(TCountryId const & countryId);
  /// \brief Downloading process could be interupted because of bad internet connection.
  /// In that case user could want to recover it. This method is done for it.
  /// This method works with expandable and not expandable countryId.
  /// \return false in case of error and true otherwise.
  bool RestoreNodeDownloading(TCountryId const & countryId);

  /// \brief Shows a node (expandable or not) on the map.
  /// \return false in case of error and true otherwise.
  bool ShowNode(TCountryId const & countryId);

  /// \brief Get information for mwm update button.
  /// \return true if updateInfo is filled correctly and false otherwise.
  bool GetUpdateInfo(UpdateInfo & updateInfo) const;
  /// \brief Update all mwm in case of changing mwm hierarchy of mwm borders.
  /// This method:
  /// * removes all mwms
  /// * downloads mwms with the same coverage
  /// \note This method is used in very rare case.
  /// \return false in case of error and true otherwise.
  bool UpdateAllAndChangeHierarchy();

  /// \brief Subscribe on change status callback.
  /// \returns a unique index of added status callback structure.
  size_t SubscribeStatusCallback(StatusCallback const & statusCallbacks);
  /// \brief Unsubscribe from change status callback.
  /// \param index is a unique index of callback retruned by SubscribeStatusCallback.
  void UnsubscribeStatusCallback(size_t index);
  //@}

  /// \returns real (not fake) local maps contained in countries.txt.
  /// So this method does not return custom user local maps and World and WorldCoosts country id.
  void GetLocalRealMaps(vector<TCountryId> & localMaps) const;

  void Init(TUpdate const & update);


  // Switch on new storage version, remove old mwm
  // and add required mwm's into download queue.
  void Migrate();

  // Clears local files registry and downloader's queue.
  void Clear();

  // Finds and registers all map files in maps directory. In the case
  // of several versions of the same map keeps only the latest one, others
  // are deleted from disk.
  // *NOTE* storage will forget all already known local maps.
  void RegisterAllLocalMaps();

  // Returns list of all local maps, including fake countries (World*.mwm).
  void GetLocalMaps(vector<TLocalFilePtr> & maps) const;
  // Returns number of downloaded maps (files), excluding fake countries (World*.mwm).
  size_t GetDownloadedFilesCount() const;

  /// @return unique identifier that should be used with Unsubscribe function
  int Subscribe(TChangeCountryFunction const & change, TProgressFunction const & progress);
  void Unsubscribe(int slotId);

  Country const & CountryByCountryId(TCountryId const & countryId) const;
  TCountryId FindCountryIdByFile(string const & name) const;
  /// @todo Temporary function to gel all associated indexes for the country file name.
  /// Will be removed in future after refactoring.
  vector<TCountryId> FindAllIndexesByFile(string const & name) const;
  void GetGroupAndCountry(TCountryId const & countryId, string & group, string & country) const;

  size_t CountriesCount(TCountryId const & countryId) const;
  string const & CountryName(TCountryId const & countryId) const;
  bool IsCoutryIdInCountryTree(TCountryId const & countryId) const;

  LocalAndRemoteSizeT CountrySizeInBytes(TCountryId const & countryId, MapOptions opt) const;
  platform::CountryFile const & GetCountryFile(TCountryId const & countryId) const;
  TLocalFilePtr GetLatestLocalFile(platform::CountryFile const & countryFile) const;
  TLocalFilePtr GetLatestLocalFile(TCountryId const & countryId) const;

  /// Fast version, doesn't check if country is out of date
  TStatus CountryStatus(TCountryId const & countryId) const;
  /// Slow version, but checks if country is out of date
  TStatus CountryStatusEx(TCountryId const & countryId) const;
  void CountryStatusEx(TCountryId const & countryId, TStatus & status, MapOptions & options) const;

  /// Puts country denoted by countryId into the downloader's queue.
  /// During downloading process notifies observers about downloading
  /// progress and status changes.
  void DownloadCountry(TCountryId const & countryId, MapOptions opt);

  /// Removes country files (for all versions) from the device.
  /// Notifies observers about country status change.
  void DeleteCountry(TCountryId const & countryId, MapOptions opt);

  /// Removes country files of a particular version from the device.
  /// Notifies observers about country status change.
  void DeleteCustomCountryVersion(platform::LocalCountryFile const & localFile);

  /// \return True iff country denoted by countryId was successfully
  ///          deleted from the downloader's queue.
  bool DeleteFromDownloader(TCountryId const & countryId);
  bool IsDownloadInProgress() const;

  TCountryId GetCurrentDownloadingCountryIndex() const;

  void NotifyStatusChanged(TCountryId const & countryId);

  /// get download url by countryId & options(first search file name by countryId, then format url)
  string GetFileDownloadUrl(string const & baseUrl, TCountryId const & countryId, MapOptions file) const;
  /// get download url by base url & file name
  string GetFileDownloadUrl(string const & baseUrl, string const & fName) const;

  /// @param[out] res Populated with oudated countries.
  void GetOutdatedCountries(vector<Country const *> & countries) const;

  void SetDownloaderForTesting(unique_ptr<MapFilesDownloader> && downloader);
  void SetCurrentDataVersionForTesting(int64_t currentVersion);

private:
  friend void UnitTest_StorageTest_DeleteCountry();

  TStatus CountryStatusWithoutFailed(TCountryId const & countryId) const;
  TStatus CountryStatusFull(TCountryId const & countryId, TStatus const status) const;

  // Modifies file set of requested files - always adds a map file
  // when routing file is requested for downloading, but drops all
  // already downloaded and up-to-date files.
  MapOptions NormalizeDownloadFileSet(TCountryId const & countryId, MapOptions options) const;

  // Modifies file set of file to deletion - always adds (marks for
  // removal) a routing file when map file is marked for deletion.
  MapOptions NormalizeDeleteFileSet(MapOptions options) const;

  // Returns a pointer to a country in the downloader's queue.
  QueuedCountry * FindCountryInQueue(TCountryId const & countryId);

  // Returns a pointer to a country in the downloader's queue.
  QueuedCountry const * FindCountryInQueue(TCountryId const & countryId) const;

  // Returns true when country is in the downloader's queue.
  bool IsCountryInQueue(TCountryId const & countryId) const;

  // Returns true when country is first in the downloader's queue.
  bool IsCountryFirstInQueue(TCountryId const & countryId) const;

  // Returns local country files of a particular version, or wrapped
  // nullptr if there're no country files corresponding to the
  // version.
  TLocalFilePtr GetLocalFile(TCountryId const & countryId, int64_t version) const;

  // Tries to register disk files for a real (listed in countries.txt)
  // country. If map files of the same version were already
  // registered, does nothing.
  void RegisterCountryFiles(TLocalFilePtr localFile);

  // Registers disk files for a country. This method must be used only
  // for real (listed in counties.txt) countries.
  void RegisterCountryFiles(TCountryId const & countryId, string const & directory, int64_t version);

  // Registers disk files for a country. This method must be used only
  // for custom (made by user) map files.
  void RegisterFakeCountryFiles(platform::LocalCountryFile const & localFile);

  // Removes disk files for all versions of a country.
  void DeleteCountryFiles(TCountryId const & countryId, MapOptions opt);

  // Removes country files from downloader.
  bool DeleteCountryFilesFromDownloader(TCountryId const & countryId, MapOptions opt);

  // Returns download size of the currently downloading file for the
  // queued country.
  uint64_t GetDownloadSize(QueuedCountry const & queuedCountry) const;

  // Returns a path to a place on disk downloader can use for
  // downloaded files.
  string GetFileDownloadPath(TCountryId const & countryId, MapOptions file) const;
};

bool HasCountryId(vector<TCountryId> const & sorted, TCountryId const & countyId);
}  // storage
