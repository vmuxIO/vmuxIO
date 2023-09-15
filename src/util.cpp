#include "src/util.hpp"
#include <dirent.h>
#include <vector> 
#include <cstring>


std::string get_iommu_group(std::string pci_device){
    std::string path = "/sys/kernel/iommu_groups/";
    struct dirent *iommu_group;
    DIR *iommu_dir = opendir(path.c_str());
    if (iommu_dir == NULL){
        return "";
    }
    while((iommu_group = readdir(iommu_dir)) != NULL) {
        if(strcmp(iommu_group->d_name,".") != 0 &&
                strcmp(iommu_group->d_name,"..") != 0){
            std::string iommu_group_str = iommu_group->d_name;
            struct dirent *iommu_group_dir;
            DIR *pci =
                opendir((path + iommu_group->d_name + "/devices").c_str());
            while((iommu_group_dir = readdir(pci)) != NULL){
                if(pci_device == iommu_group_dir->d_name){
                    closedir(pci);
                    closedir(iommu_dir);
                    return iommu_group_str;
                }
            }
            closedir(pci);
        }
    }
    closedir(iommu_dir);
    return "";
}

std::vector<int> get_hardware_ids(std::string pci_device,
        std::string iommu_group)
{
    std::string path = "/sys/kernel/iommu_groups/"
        + iommu_group +"/devices/" + pci_device + "/";
    std::vector<std::string> values = {"revision", "vendor", "device",
        "subsystem_vendor", "subsystem_device"};
    std::vector<int> result;
    int bytes_read;
    char id_buffer[7] = {0};
    FILE* id;

    for(size_t i = 0; i < values.size(); i++ ){
        id = fopen((path + values[i]).c_str(), "r");
        if(id == NULL){
            result.clear();
            printf("Failed to open iommu sysfs file: %s\n",(path + values[i]).c_str());
            return result;
        }
        bytes_read = fread(id_buffer, 1, sizeof(id_buffer) /
                sizeof(id_buffer[0]) - 1, id);
        if(bytes_read < 1){
            result.clear();
            printf("Failed to read %s, got %s\n", values[i].c_str(),id_buffer);
            return result;
        }
        result.push_back((int)strtol(id_buffer,NULL,0));
        fclose(id);
    }   

    return result;
}

/* convert simbricks bar flags (SIMBRICKS_PROTO_PCIE_BAR_*) to vfio-user flags (VFU_REGION_FLAG_*) */
int convert_flags(int bricks) {
    int vfu = 0;
    
    // if BAR_IO (port io) is not set, it is FLAG_MEM (MMIO)
    if (!(bricks & SIMBRICKS_PROTO_PCIE_BAR_IO)) {
        vfu |= VFU_REGION_FLAG_MEM;
    }

    return vfu;
}
