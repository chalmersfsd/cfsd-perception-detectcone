/*
    Copyright (c) 2013, Taiga Nomi and the respective contributors
    All rights reserved.

    Use of this source code is governed by a BSD-style license that can be found
    in the LICENSE file.
*/
#include <cstdlib>
#include <iostream>
#include <vector>

#include "tiny_dnn/tiny_dnn.h"

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <boost/foreach.hpp>
#include <boost/filesystem.hpp>

int patch_size = 64;

void convert_image(cv::Mat img, int w, int h, tiny_dnn::vec_t& data){
  cv::Mat resized;
  cv::resize(img, resized, cv::Size(w, h));
  data.resize(w * h * 3);
  for (int c = 0; c < 3; ++c) {
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
       data[c * w * h + y * w + x] =
         float(resized.at<cv::Vec3b>(y, x)[c] / 255.0);
      }
    }
  }
}

// convert all images found in directory to vec_t
void load_data(const std::string& directory,
                    int w,
                    int h,
                    std::vector<tiny_dnn::vec_t>& train_imgs,
                    std::vector<tiny_dnn::label_t>& train_labels,
                    std::vector<tiny_dnn::vec_t>& train_values,
                    std::vector<tiny_dnn::vec_t>& test_imgs,
                    std::vector<tiny_dnn::label_t>& test_labels,
                    std::vector<tiny_dnn::vec_t>& test_values)
{
    boost::filesystem::path trainPath(directory+"/train");
    boost::filesystem::path testPath(directory+"/test");
    int label;

    tiny_dnn::vec_t data, value;

    BOOST_FOREACH(const boost::filesystem::path& labelPath, std::make_pair(boost::filesystem::directory_iterator(trainPath), boost::filesystem::directory_iterator())) {
        //if (is_directory(p)) continue;
        BOOST_FOREACH(const boost::filesystem::path& imgPath, std::make_pair(boost::filesystem::directory_iterator(labelPath), boost::filesystem::directory_iterator())) {
          label = stoi(labelPath.filename().string());
          value = {0,0,0,0};
          value[label] = 1;
          auto img = cv::imread(imgPath.string());

          convert_image(img, w, h, data);
          train_values.push_back(value);
          train_labels.push_back(label);
          train_imgs.push_back(data);
      }
    }
    BOOST_FOREACH(const boost::filesystem::path& labelPath, std::make_pair(boost::filesystem::directory_iterator(testPath), boost::filesystem::directory_iterator())) {
        //if (is_directory(p)) continue;
        BOOST_FOREACH(const boost::filesystem::path& imgPath, std::make_pair(boost::filesystem::directory_iterator(labelPath), boost::filesystem::directory_iterator())) {
          label = stoi(labelPath.filename().string());
          value = {0,0,0,0};
          value[label] = 1;
          auto img = cv::imread(imgPath.string());

          convert_image(img, w, h, data);
          test_values.push_back(value);
          test_labels.push_back(label);
          test_imgs.push_back(data);
      }
    }
    std::cout << "loaded data" << std::endl;
}

template <typename N>
void construct_net(N &nn, tiny_dnn::core::backend_t backend_type) {
  using conv    = tiny_dnn::convolutional_layer;
  using pool    = tiny_dnn::max_pooling_layer;
  using fc      = tiny_dnn::fully_connected_layer;
  using tanh    = tiny_dnn::tanh_layer;
  using relu    = tiny_dnn::relu_layer;
  using softmax = tiny_dnn::softmax_layer;
  using dropout = tiny_dnn::dropout_layer;

  nn << conv(64, 64, 4, 3, 16, tiny_dnn::padding::valid, true, 2, 2, 1, 1, backend_type) << tanh() 
     << dropout(31*31*16, 0.25)                                                   
     << conv(31, 31, 3, 16, 16, tiny_dnn::padding::valid, true, 2, 2, 1, 1, backend_type) << tanh() 
     << dropout(15*15*16, 0.25)
     << conv(15, 15, 3, 16, 32, tiny_dnn::padding::valid, true, 2, 2, 1, 1, backend_type) << tanh() 
     << dropout(7*7*32, 0.25)
     << conv(7, 7, 3, 32, 32, tiny_dnn::padding::valid, true, 2, 2, 1, 1, backend_type) << tanh() 
     << dropout(3*3*32, 0.25)                     
     << fc(3 * 3 * 32, 128, true, backend_type) << relu()  
     << fc(128, 4, true, backend_type) << softmax(4);

   for (int i = 0; i < nn.depth(); i++) {
        std::cout << "#layer:" << i << "\n";
        std::cout << "layer type:" << nn[i]->layer_type() << "\n";
        std::cout << "input:" << nn[i]->in_size() << "(" << nn[i]->in_shape() << ")\n";
        std::cout << "output:" << nn[i]->out_size() << "(" << nn[i]->out_shape() << ")\n";
    }
}

void train_network(std::string data_path,
                   std::string model_path,
                   double learning_rate,
                   int n_train_epochs,
                   int n_minibatch) {
  // specify loss-function and learning strategy
  tiny_dnn::network<tiny_dnn::sequential> nn;
  tiny_dnn::adam optimizer;

  construct_net(nn, tiny_dnn::core::backend_t::internal);

  // std::ifstream ifs("efficient_sliding_window");
  // ifs >> nn;

  std::vector<tiny_dnn::vec_t> train_values, test_values, train_images, test_images;
  std::vector<tiny_dnn::label_t> train_labels, test_labels;

  load_data(data_path, patch_size, patch_size, train_images, train_labels, train_values, test_images, test_labels, test_values);

  std::cout << "start learning" << std::endl;

  tiny_dnn::progress_display disp(train_images.size());
  tiny_dnn::timer t;

  optimizer.alpha *= static_cast<float_t>(sqrt(n_minibatch) * learning_rate);

  int epoch = 1;
  int loss_val_temp = 10000;
  // create callback
  auto on_enumerate_epoch = [&]() {
    std::cout << "Epoch " << epoch << "/" << n_train_epochs << " finished. "
              << t.elapsed() << "s elapsed." << std::endl;

    // tiny_dnn::result train_res = nn.test(train_images, train_labels);
    // float_t loss_train = nn.get_loss<tiny_dnn::cross_entropy_multiclass>(train_images, train_values);
    // std::cout << "Training accuracy: " << train_res.num_success << "/" << train_res.num_total << " = " << 100.0*train_res.num_success/train_res.num_total << "%, loss: " << loss_train << std::endl;
    
    if (epoch%20 == 1){
      tiny_dnn::result test_res = nn.test(test_images, test_labels);
      std::cout << "Validation accuracy: " <<test_res.num_success << "/" << test_res.num_total << " = " << 100.0*test_res.num_success/test_res.num_total << "%" << std::endl;
    }
    if (epoch%5 == 1){
      float_t loss_val = nn.get_loss<tiny_dnn::cross_entropy_multiclass>(test_images, test_values);
      std::cout << "Validation loss: " << loss_val/test_images.size() << std::endl;
      if(loss_val < 0){
        std::cout << "Training crash!" << std::endl;
        return;
      }
      if(loss_val < loss_val_temp){
        loss_val_temp = loss_val;
        std::ofstream ofs (model_path);
        ofs << nn;
      }
    }
    ++epoch;

    disp.restart(train_images.size());
    t.restart();
  };

  auto on_enumerate_minibatch = [&]() { disp += n_minibatch; };

  // training
  nn.fit<tiny_dnn::cross_entropy_multiclass>(optimizer, train_images, train_values,
                                    n_minibatch, n_train_epochs,
                                    on_enumerate_minibatch, on_enumerate_epoch);

  std::cout << "end training." << std::endl;

  // test and show results
  nn.test(test_images, test_labels).print_detail(std::cout);
}

int main(int argc, char **argv) {
  train_network(argv[1], argv[2], std::stod(argv[3]), std::stoi(argv[4]), std::stoi(argv[5]));
}