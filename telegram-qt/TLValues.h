/*
    Copyright (C) 2014 Alexandr Akulich <akulichalexander@gmail.com>

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
    NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
    LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
    OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
    WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#ifndef TLVALUES_H
#define TLVALUES_H

enum TLValues {
    // https://core.telegram.org/mtproto/auth_key
    ReqPQ = 0x60469778,
    ResPQ = 0x05162463,
    ReqDHParams = 0xd712e4be,

    // https://core.telegram.org/schema by generator
    BoolFalse = 0xbc799737,
    BoolTrue = 0x997275b5,
    Vector = 0x1cb5c415,
    Error = 0xc4b9f9bb,
    Null = 0x56730bcc,
    InputPeerEmpty = 0x7f3b18ea,
    InputPeerSelf = 0x7da07ec9,
    InputPeerContact = 0x1023dbe8,
    InputPeerForeign = 0x9b447325,
    InputPeerChat = 0x179be863,
    InputUserEmpty = 0xb98886cf,
    InputUserSelf = 0xf7c1b13f,
    InputUserContact = 0x86e94f65,
    InputUserForeign = 0x655e74ff,
    InputPhoneContact = 0xf392b7f4,
    InputFile = 0xf52ff27f,
    InputMediaEmpty = 0x9664f57f,
    InputMediaUploadedPhoto = 0x2dc53a7d,
    InputMediaPhoto = 0x8f2ab2ec,
    InputMediaGeoPoint = 0xf9c44144,
    InputMediaContact = 0xa6e45987,
    InputMediaUploadedVideo = 0x133ad6f6,
    InputMediaUploadedThumbVideo = 0x9912dabf,
    InputMediaVideo = 0x7f023ae6,
    InputChatPhotoEmpty = 0x1ca48f57,
    InputChatUploadedPhoto = 0x94254732,
    InputChatPhoto = 0xb2e1bf08,
    InputGeoPointEmpty = 0xe4c123d6,
    InputGeoPoint = 0xf3b7acc9,
    InputPhotoEmpty = 0x1cd7bf0d,
    InputPhoto = 0xfb95c6c4,
    InputVideoEmpty = 0x5508ec75,
    InputVideo = 0xee579652,
    InputFileLocation = 0x14637196,
    InputVideoFileLocation = 0x3d0364ec,
    InputPhotoCropAuto = 0xade6b004,
    InputPhotoCrop = 0xd9915325,
    InputAppEvent = 0x770656a8,
    PeerUser = 0x9db1bc6d,
    PeerChat = 0xbad0e5bb,
    StorageFileUnknown = 0xaa963b05,
    StorageFileJpeg = 0x7efe0e,
    StorageFileGif = 0xcae1aadf,
    StorageFilePng = 0xa4f63c0,
    StorageFilePdf = 0xae1e508d,
    StorageFileMp3 = 0x528a0677,
    StorageFileMov = 0x4b09ebbc,
    StorageFilePartial = 0x40bc6f52,
    StorageFileMp4 = 0xb3cea0e4,
    StorageFileWebp = 0x1081464c,
    FileLocationUnavailable = 0x7c596b46,
    FileLocation = 0x53d69076,
    UserEmpty = 0x200250ba,
    UserSelf = 0x720535ec,
    UserContact = 0xf2fb8319,
    UserRequest = 0x22e8ceb0,
    UserForeign = 0x5214c89d,
    UserDeleted = 0xb29ad7cc,
    UserProfilePhotoEmpty = 0x4f11bae1,
    UserProfilePhoto = 0xd559d8c8,
    UserStatusEmpty = 0x9d05049,
    UserStatusOnline = 0xedb93949,
    UserStatusOffline = 0x8c703f,
    ChatEmpty = 0x9ba2d800,
    Chat = 0x6e9c9bc7,
    ChatForbidden = 0xfb0ccc41,
    ChatFull = 0x630e61be,
    ChatParticipant = 0xc8d7493e,
    ChatParticipantsForbidden = 0xfd2bb8a,
    ChatParticipants = 0x7841b415,
    ChatPhotoEmpty = 0x37c1011c,
    ChatPhoto = 0x6153276a,
    MessageEmpty = 0x83e5de54,
    Message = 0x22eb6aba,
    MessageForwarded = 0x5f46804,
    MessageService = 0x9f8d60bb,
    MessageMediaEmpty = 0x3ded6320,
    MessageMediaPhoto = 0xc8c45a2a,
    MessageMediaVideo = 0xa2d24290,
    MessageMediaGeo = 0x56e0d474,
    MessageMediaContact = 0x5e7d2f39,
    MessageMediaUnsupported = 0x29632a36,
    MessageActionEmpty = 0xb6aef7b0,
    MessageActionChatCreate = 0xa6638b9a,
    MessageActionChatEditTitle = 0xb5a1ce5a,
    MessageActionChatEditPhoto = 0x7fcb13a8,
    MessageActionChatDeletePhoto = 0x95e3fbef,
    MessageActionChatAddUser = 0x5e3cfc4b,
    MessageActionChatDeleteUser = 0xb2ae9b0c,
    Dialog = 0x214a8cdf,
    PhotoEmpty = 0x2331b22d,
    Photo = 0x22b56751,
    PhotoSizeEmpty = 0xe17e23c,
    PhotoSize = 0x77bfb61b,
    PhotoCachedSize = 0xe9a734fa,
    VideoEmpty = 0xc10658a8,
    Video = 0x388fa391,
    GeoPointEmpty = 0x1117dd5f,
    GeoPoint = 0x2049d70c,
    AuthCheckedPhone = 0xe300cc3b,
    AuthSentCode = 0xefed51d9,
    AuthAuthorization = 0xf6b673a4,
    AuthExportedAuthorization = 0xdf969c2d,
    InputNotifyPeer = 0xb8bc5b0c,
    InputNotifyUsers = 0x193b4417,
    InputNotifyChats = 0x4a95e84e,
    InputNotifyAll = 0xa429b886,
    InputPeerNotifyEventsEmpty = 0xf03064d8,
    InputPeerNotifyEventsAll = 0xe86a2c74,
    InputPeerNotifySettings = 0x46a2ce98,
    PeerNotifyEventsEmpty = 0xadd53cb3,
    PeerNotifyEventsAll = 0x6d1ded88,
    PeerNotifySettingsEmpty = 0x70a68512,
    PeerNotifySettings = 0x8d5e11ee,
    WallPaper = 0xccb03657,
    UserFull = 0x771095da,
    Contact = 0xf911c994,
    ImportedContact = 0xd0028438,
    ContactBlocked = 0x561bc879,
    ContactFound = 0xea879f95,
    ContactSuggested = 0x3de191a1,
    ContactStatus = 0xaa77b873,
    ChatLocated = 0x3631cf4c,
    ContactsForeignLinkUnknown = 0x133421f8,
    ContactsForeignLinkRequested = 0xa7801f47,
    ContactsForeignLinkMutual = 0x1bea8ce1,
    ContactsMyLinkEmpty = 0xd22a1c60,
    ContactsMyLinkRequested = 0x6c69efee,
    ContactsMyLinkContact = 0xc240ebd9,
    ContactsLink = 0xeccea3f5,
    ContactsContacts = 0x6f8b8cb2,
    ContactsContactsNotModified = 0xb74ba9d2,
    ContactsImportedContacts = 0xad524315,
    ContactsBlocked = 0x1c138d15,
    ContactsBlockedSlice = 0x900802a1,
    ContactsFound = 0x566000e,
    ContactsSuggested = 0x5649dcc5,
    MessagesDialogs = 0x15ba6c40,
    MessagesDialogsSlice = 0x71e094f3,
    MessagesMessages = 0x8c718e87,
    MessagesMessagesSlice = 0xb446ae3,
    MessagesMessageEmpty = 0x3f4e0648,
    MessagesMessage = 0xff90c417,
    MessagesStatedMessages = 0x969478bb,
    MessagesStatedMessage = 0xd07ae726,
    MessagesSentMessage = 0xd1f4d35c,
    MessagesChat = 0x40e9002a,
    MessagesChats = 0x8150cbd8,
    MessagesChatFull = 0xe5d7d19c,
    MessagesAffectedHistory = 0xb7de36f2,
    InputMessagesFilterEmpty = 0x57e2f66c,
    InputMessagesFilterPhotos = 0x9609a51c,
    InputMessagesFilterVideo = 0x9fc00e65,
    InputMessagesFilterPhotoVideo = 0x56e9f0e4,
    InputMessagesFilterDocument = 0x9eddf188,
    UpdateNewMessage = 0x13abdb3,
    UpdateMessageID = 0x4e90bfd6,
    UpdateReadMessages = 0xc6649e31,
    UpdateDeleteMessages = 0xa92bfe26,
    UpdateRestoreMessages = 0xd15de04d,
    UpdateUserTyping = 0x6baa8508,
    UpdateChatUserTyping = 0x3c46cfe6,
    UpdateChatParticipants = 0x7761198,
    UpdateUserStatus = 0x1bfbd823,
    UpdateUserName = 0xda22d9ad,
    UpdateUserPhoto = 0x95313b0c,
    UpdateContactRegistered = 0x2575bbb9,
    UpdateContactLink = 0x51a48a9a,
    UpdateActivation = 0x6f690963,
    UpdateNewAuthorization = 0x8f06529a,
    UpdatesState = 0xa56c2a3e,
    UpdatesDifferenceEmpty = 0x5d75a138,
    UpdatesDifference = 0xf49ca0,
    UpdatesDifferenceSlice = 0xa8fb1981,
    UpdatesTooLong = 0xe317af7e,
    UpdateShortMessage = 0xd3f45784,
    UpdateShortChatMessage = 0x2b2fbd4e,
    UpdateShort = 0x78d4dec1,
    UpdatesCombined = 0x725b04c3,
    Updates = 0x74ae4240,
    PhotosPhotos = 0x8dca6aa5,
    PhotosPhotosSlice = 0x15051f54,
    PhotosPhoto = 0x20212ca8,
    UploadFile = 0x96a18d5,
    DcOption = 0x2ec2a43c,
    Config = 0x2e54dd74,
    NearestDc = 0x8e1a1775,
    HelpAppUpdate = 0x8987f311,
    HelpNoAppUpdate = 0xc45a6536,
    HelpInviteText = 0x18cb9f78,
    MessagesStatedMessagesLinks = 0x3e74f5c6,
    MessagesStatedMessageLink = 0xa9af2881,
    MessagesSentMessageLink = 0xe9db4a3f,
    InputGeoChat = 0x74d456fa,
    InputNotifyGeoChatPeer = 0x4d8ddec8,
    GeoChat = 0x75eaea5a,
    GeoChatMessageEmpty = 0x60311a9b,
    GeoChatMessage = 0x4505f8e1,
    GeoChatMessageService = 0xd34fa24e,
    GeochatsStatedMessage = 0x17b1578b,
    GeochatsLocated = 0x48feb267,
    GeochatsMessages = 0xd1526db1,
    GeochatsMessagesSlice = 0xbc5863e8,
    MessageActionGeoChatCreate = 0x6f038ebc,
    MessageActionGeoChatCheckin = 0xc7d53de,
    UpdateNewGeoChatMessage = 0x5a68e3f7,
    WallPaperSolid = 0x63117f24,
    UpdateNewEncryptedMessage = 0x12bcbd9a,
    UpdateEncryptedChatTyping = 0x1710f156,
    UpdateEncryption = 0xb4a2e88d,
    UpdateEncryptedMessagesRead = 0x38fe25b7,
    EncryptedChatEmpty = 0xab7ec0a0,
    EncryptedChatWaiting = 0x3bf703dc,
    EncryptedChatRequested = 0xc878527e,
    EncryptedChat = 0xfa56ce36,
    EncryptedChatDiscarded = 0x13d6dd27,
    InputEncryptedChat = 0xf141b5e1,
    EncryptedFileEmpty = 0xc21f497e,
    EncryptedFile = 0x4a70994c,
    InputEncryptedFileEmpty = 0x1837c364,
    InputEncryptedFileUploaded = 0x64bd0306,
    InputEncryptedFile = 0x5a17b5e5,
    InputEncryptedFileLocation = 0xf5235d55,
    EncryptedMessage = 0xed18c118,
    EncryptedMessageService = 0x23734b06,
    DecryptedMessageLayer = 0x99a438cf,
    DecryptedMessage = 0x1f814f1f,
    DecryptedMessageService = 0xaa48327d,
    DecryptedMessageMediaEmpty = 0x89f5c4a,
    DecryptedMessageMediaPhoto = 0x32798a8c,
    DecryptedMessageMediaVideo = 0x524a415d,
    DecryptedMessageMediaGeoPoint = 0x35480a59,
    DecryptedMessageMediaContact = 0x588a0a97,
    DecryptedMessageActionSetMessageTTL = 0xa1733aec,
    MessagesDhConfigNotModified = 0xc0e24635,
    MessagesDhConfig = 0x2c221edd,
    MessagesSentEncryptedMessage = 0x560f8935,
    MessagesSentEncryptedFile = 0x9493ff32,
    InputFileBig = 0xfa4f0bb5,
    InputEncryptedFileBigUploaded = 0x2dc173c8,
    UpdateChatParticipantAdd = 0x3a0eeb22,
    UpdateChatParticipantDelete = 0x6e5f8c22,
    UpdateDcOptions = 0x8e5e9873,
    InputMediaUploadedAudio = 0x4e498cab,
    InputMediaAudio = 0x89938781,
    InputMediaUploadedDocument = 0x34e794bd,
    InputMediaUploadedThumbDocument = 0x3e46de5d,
    InputMediaDocument = 0xd184e841,
    MessageMediaDocument = 0x2fda2204,
    MessageMediaAudio = 0xc6b68300,
    InputAudioEmpty = 0xd95adc84,
    InputAudio = 0x77d440ff,
    InputDocumentEmpty = 0x72f0eaae,
    InputDocument = 0x18798952,
    InputAudioFileLocation = 0x74dc404d,
    InputDocumentFileLocation = 0x4e45abe9,
    DecryptedMessageMediaDocument = 0xb095434b,
    DecryptedMessageMediaAudio = 0x57e0a9cb,
    AudioEmpty = 0x586988d8,
    Audio = 0xc7ac6496,
    DocumentEmpty = 0x36f8c871,
    Document = 0x9efc6326,
    HelpSupport = 0x17c6b5f6,
    DecryptedMessageActionReadMessages = 0xc4f40be,
    DecryptedMessageActionDeleteMessages = 0x65614304,
    DecryptedMessageActionFlushHistory = 0x6719e45c,
    DecryptedMessageActionNotifyLayer = 0xf3048883,
    InvokeAfterMsg = 0xcb9f372d,
    InvokeAfterMsgs = 0x3dc4b4f0,
    AuthCheckPhone = 0x6fe51dfb,
    AuthSendCode = 0x768d5f4d,
    AuthSendCall = 0x3c51564,
    AuthSignUp = 0x1b067634,
    AuthSignIn = 0xbcd51581,
    AuthLogOut = 0x5717da40,
    AuthResetAuthorizations = 0x9fab0d1a,
    AuthSendInvites = 0x771c1d97,
    AuthExportAuthorization = 0xe5bfffcd,
    AuthImportAuthorization = 0xe3ef9613,
    AccountRegisterDevice = 0x446c712c,
    AccountUnregisterDevice = 0x65c55b40,
    AccountUpdateNotifySettings = 0x84be5b93,
    AccountGetNotifySettings = 0x12b3ad31,
    AccountResetNotifySettings = 0xdb7e1747,
    AccountUpdateProfile = 0xf0888d68,
    AccountUpdateStatus = 0x6628562c,
    AccountGetWallPapers = 0xc04cfac2,
    UsersGetUsers = 0xd91a548,
    UsersGetFullUser = 0xca30a5b1,
    ContactsGetStatuses = 0xc4a353ee,
    ContactsGetContacts = 0x22c6aa08,
    ContactsImportContacts = 0xda30b32d,
    ContactsSearch = 0x11f812d8,
    ContactsGetSuggested = 0xcd773428,
    ContactsDeleteContact = 0x8e953744,
    ContactsDeleteContacts = 0x59ab389e,
    ContactsBlock = 0x332b49fc,
    ContactsUnblock = 0xe54100bd,
    ContactsGetBlocked = 0xf57c350f,
    MessagesGetMessages = 0x4222fa74,
    MessagesGetDialogs = 0xeccf1df6,
    MessagesGetHistory = 0x92a1df2f,
    MessagesSearch = 0x7e9f2ab,
    MessagesReadHistory = 0xb04f2510,
    MessagesDeleteHistory = 0xf4f8fb61,
    MessagesDeleteMessages = 0x14f2dd0a,
    MessagesRestoreMessages = 0x395f9d7e,
    MessagesReceivedMessages = 0x28abcb68,
    MessagesSetTyping = 0x719839e9,
    MessagesSendMessage = 0x4cde0aab,
    MessagesSendMedia = 0xa3c85d76,
    MessagesForwardMessages = 0x514cd10f,
    MessagesGetChats = 0x3c6aa187,
    MessagesGetFullChat = 0x3b831c66,
    MessagesEditChatTitle = 0xb4bc68b5,
    MessagesEditChatPhoto = 0xd881821d,
    MessagesAddChatUser = 0x2ee9ee9e,
    MessagesDeleteChatUser = 0xc3c5cd23,
    MessagesCreateChat = 0x419d9aee,
    UpdatesGetState = 0xedd4882a,
    UpdatesGetDifference = 0xa041495,
    PhotosUpdateProfilePhoto = 0xeef579a0,
    PhotosUploadProfilePhoto = 0xd50f9c88,
    UploadSaveFilePart = 0xb304a621,
    UploadGetFile = 0xe3a6cfb5,
    HelpGetConfig = 0xc4f9186b,
    HelpGetNearestDc = 0x1fb33026,
    HelpGetAppUpdate = 0xc812ac7e,
    HelpSaveAppLog = 0x6f02f748,
    HelpGetInviteText = 0xa4a95186,
    PhotosGetUserPhotos = 0xb7ee553c,
    MessagesForwardMessage = 0x3f3f4f2,
    MessagesSendBroadcast = 0x41bb0972,
    GeochatsGetLocated = 0x7f192d8f,
    GeochatsGetRecents = 0xe1427e6f,
    GeochatsCheckin = 0x55b3e8fb,
    GeochatsGetFullChat = 0x6722dd6f,
    GeochatsEditChatTitle = 0x4c8e2273,
    GeochatsEditChatPhoto = 0x35d81a95,
    GeochatsSearch = 0xcfcdc44d,
    GeochatsGetHistory = 0xb53f7a68,
    GeochatsSetTyping = 0x8b8a729,
    GeochatsSendMessage = 0x61b0044,
    GeochatsSendMedia = 0xb8f0deff,
    GeochatsCreateGeoChat = 0xe092e16,
    MessagesGetDhConfig = 0x26cf8950,
    MessagesRequestEncryption = 0xf64daf43,
    MessagesAcceptEncryption = 0x3dbc0415,
    MessagesDiscardEncryption = 0xedd923c5,
    MessagesSetEncryptedTyping = 0x791451ed,
    MessagesReadEncryptedHistory = 0x7f4b690a,
    MessagesSendEncrypted = 0xa9776773,
    MessagesSendEncryptedFile = 0x9a901b66,
    MessagesSendEncryptedService = 0x32d439a4,
    MessagesReceivedQueue = 0x55a5bb66,
    UploadSaveBigFilePart = 0xde7b673d,
    InitConnection = 0x69796de9,
    HelpGetSupport = 0x9cdf08cd,
    InvokeWithLayer13 = 0x427c8ea2
};

#endif // TLVALUES_H
